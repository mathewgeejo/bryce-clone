#include "scene.h"

#include <map>

namespace {

bool finiteV(V3 v) {
    return std::fabs(v.x) <= 1e6f && std::fabs(v.y) <= 1e6f && std::fabs(v.z) <= 1e6f;
}

bool validKey(const Key& key) {
    return key.frame >= 0 && key.frame < 100000 && finiteV(key.pos) && finiteV(key.rot) && finiteV(key.scl) &&
           key.scl.x >= .01f && key.scl.y >= .01f && key.scl.z >= .01f && key.scl.x <= 100 && key.scl.y <= 100 &&
           key.scl.z <= 100 && key.ease >= Ease::Spline && key.ease < Ease::COUNT;
}

bool validGoal(const IKKey& key) {
    return key.frame >= 0 && key.frame < 100000 && finiteV(key.target) && finiteV(key.pole) &&
           key.influence >= 0 && key.influence <= 1 && key.ease >= Ease::Spline && key.ease < Ease::COUNT;
}

M3 restBasis(V3 direction) {
    V3 y = normalize(direction);
    V3 x = normalize(cross(y, std::fabs(y.z) < .9f ? V3(0, 0, 1) : V3(1, 0, 0)));
    V3 z = cross(x, y);
    M3 m;
    for (int i = 0; i < 3; ++i) {
        m.m[i][0] = x[i];
        m.m[i][1] = y[i];
        m.m[i][2] = z[i];
    }
    return m;
}

V3 perpendicular(V3 direction) {
    return normalize(cross(direction, std::fabs(direction.z) < .9f ? V3(0, 0, 1) : V3(1, 0, 0)));
}

M3 turnTowards(V3 from, V3 to, float influence) {
    from = normalize(from);
    to = normalize(to);
    V3 axis = cross(from, to);
    float sine = length(axis);
    float cosine = clampf(dot(from, to), -1, 1);
    if (sine < 1e-7f && cosine >= 0) return {};
    axis = sine > 1e-7f ? axis / sine : perpendicular(from);
    float angle = std::atan2(sine, cosine) * influence;
    float c = std::cos(angle), s = std::sin(angle);
    M3 rotation;
    const V3 axes[] = {{1,0,0}, {0,1,0}, {0,0,1}};
    for (int i = 0; i < 3; ++i) {
        V3 column = axes[i] * c + cross(axis, axes[i]) * s + axis * (axis[i] * (1 - c));
        for (int row = 0; row < 3; ++row) rotation.m[row][i] = column[row];
    }
    return rotation;
}

void rotateSubtree(const Rig& rig, std::vector<PosedJoint>& posed, int root, const M3& rotation) {
    V3 pivot = posed[root].head;
    std::array<bool, MAX_RIG_JOINTS> affected{};
    affected[root] = true;
    for (int i = root; i < (int)posed.size(); ++i) {
        int parent = rig.joints[i].parent;
        if (i != root && (parent < 0 || !affected[parent])) continue;
        affected[i] = true;
        auto& joint = posed[i];
        joint.head = pivot + rotation * (joint.head - pivot);
        joint.tail = pivot + rotation * (joint.tail - pivot);
        joint.basis = rotation * joint.basis;
    }
}

void applyIK(const Rig& rig, std::vector<PosedJoint>& posed, const RigIK& chain, const IKKey& goal) {
    if (goal.influence <= 0) return;
    int root = rig.joints[chain.end].parent;
    V3 origin = posed[root].head;
    V3 upper = posed[chain.end].head - origin;
    V3 lower = posed[chain.end].tail - posed[chain.end].head;
    float a = length(upper), b = length(lower);
    if (a < 1e-6f || b < 1e-6f) return;
    V3 reach = goal.target - origin;
    float distance = length(reach);
    V3 aim = distance > 1e-6f ? reach / distance : normalize(upper);
    float epsilon = std::min(a, b) * 1e-5f;
    float d = clampf(distance, std::fabs(a - b) + epsilon, a + b);
    V3 pole = goal.pole - origin;
    V3 side = pole - aim * dot(pole, aim);
    if (length(side) < 1e-6f) side = upper - aim * dot(upper, aim);
    side = length(side) > 1e-6f ? normalize(side) : perpendicular(aim);
    float along = clampf((a * a - b * b + d * d) / (2 * d), -a, a);
    V3 elbow = aim * along + side * std::sqrt(std::max(0.f, a * a - along * along));
    V3 end = origin + aim * d;
    rotateSubtree(rig, posed, root, turnTowards(upper, elbow, goal.influence));
    V3 moved = posed[chain.end].head;
    rotateSubtree(rig, posed, chain.end, turnTowards(posed[chain.end].tail - moved, end - moved, goal.influence));
}

struct RigFrame {
    std::vector<Key> joints;
    std::vector<IKKey> controls;
};

RigFrame sampleRigFrame(const Rig& rig, int frame) {
    RigFrame result;
    result.joints.reserve(rig.joints.size());
    result.controls.reserve(rig.ik.size());
    for (const auto& joint : rig.joints) result.joints.push_back(sampleTransformTrack(joint.keys, (float)frame, joint.pose));
    for (const auto& chain : rig.ik) result.controls.push_back(sampleIK(chain, (float)frame));
    if (rig.clips.empty()) return result;
    std::array<int, MAX_RIG_JOINTS> controlIndex{};
    controlIndex.fill(-1);
    for (size_t i = 0; i < rig.ik.size(); ++i) controlIndex[rig.ik[i].end] = (int)i;
    for (const auto& clip : rig.clips) {
        if (frame < clip.start || clip.influence <= 0) continue;
        const RigAction& action = *rig.actions[clip.action];
        int count = rigClipFrames(action, clip);
        int elapsed = frame - clip.start;
        if (elapsed >= count) continue;
        float influence = clip.influence;
        if (clip.fadeIn) influence *= smoothstep(0, (float)clip.fadeIn, (float)elapsed);
        if (clip.fadeOut) influence *= smoothstep(0, (float)clip.fadeOut, (float)(count - 1 - elapsed));
        if (influence <= 0) continue;
        float time = (float)std::fmod(elapsed * (double)clip.speed, (double)action.frames);
        for (const auto& track : action.joints) {
            Key sample = sampleTransformTrack(track.keys, time, track.keys.front());
            auto& value = result.joints[track.joint];
            if (influence >= 1) { value = sample; continue; }
            value.pos = lerp(value.pos, sample.pos, influence);
            value.scl = lerp(value.scl, sample.scl, influence);
            V3 delta = sample.rot - value.rot;
            value.rot += V3(std::remainder(delta.x, 360.f), std::remainder(delta.y, 360.f),
                            std::remainder(delta.z, 360.f)) * influence;
        }
        for (const auto& track : action.ik) {
            IKKey sample = sampleIKTrack(track.keys, time, track.keys.front());
            auto& value = result.controls[controlIndex[track.end]];
            if (influence >= 1) { value = sample; continue; }
            value.target = lerp(value.target, sample.target, influence);
            value.pole = lerp(value.pole, sample.pole, influence);
            value.influence = lerpf(value.influence, sample.influence, influence);
        }
    }
    return result;
}

size_t countSamples(const RigAction& action) {
    size_t total = 0;
    for (const auto& track : action.joints) total += track.keys.size();
    for (const auto& track : action.ik) total += track.keys.size();
    return total;
}

size_t countSamples(const Rig& rig) {
    size_t total = 0;
    for (const auto& action : rig.actions) total += countSamples(*action);
    return total;
}

}  // namespace

int rigClipFrames(const RigAction& action, const RigClip& clip) {
    if (action.frames < 1 || action.frames > MAX_ACTION_FRAMES || clip.repeats < 1 || clip.repeats > 64 ||
        !(clip.speed >= .125f && clip.speed <= 8.f)) return 0;
    return (int)std::ceil(action.frames * (double)clip.repeats / clip.speed);
}

bool validRigMotion(const Rig& rig) {
    if (rig.joints.size() > MAX_RIG_JOINTS || rig.actions.size() > MAX_RIG_ACTIONS ||
        rig.clips.size() > MAX_RIG_CLIPS) return false;
    std::array<bool, MAX_RIG_JOINTS> ikEnds{};
    for (const auto& chain : rig.ik) {
        if (chain.end < 0 || chain.end >= (int)rig.joints.size()) return false;
        ikEnds[chain.end] = true;
    }
    size_t total = 0;
    auto validTrack = [](const auto& keys, int frames, auto validate) {
        if (keys.empty() || keys.size() > (size_t)frames) return false;
        int last = -1;
        for (const auto& key : keys) {
            if (!validate(key) || key.frame <= last || key.frame >= frames) return false;
            last = key.frame;
        }
        return true;
    };
    for (const auto& action : rig.actions) {
        if (!action || action->name.empty() || action->name.size() > 120 ||
            action->frames < 1 || action->frames > MAX_ACTION_FRAMES ||
            (action->joints.empty() && action->ik.empty())) return false;
        std::array<bool, MAX_RIG_JOINTS> joints{}, controls{};
        for (const auto& track : action->joints) {
            if (track.joint < 0 || track.joint >= (int)rig.joints.size() || joints[track.joint] ||
                !validTrack(track.keys, action->frames, validKey)) return false;
            joints[track.joint] = true;
        }
        for (const auto& track : action->ik) {
            if (track.end < 0 || track.end >= (int)rig.joints.size() || !ikEnds[track.end] || controls[track.end] ||
                !validTrack(track.keys, action->frames, validGoal)) return false;
            controls[track.end] = true;
        }
        total += countSamples(*action);
        if (total > MAX_ACTION_SAMPLES) return false;
    }
    for (const auto& clip : rig.clips) {
        if (clip.action < 0 || clip.action >= (int)rig.actions.size() || clip.start < 0 ||
            !(clip.influence >= 0 && clip.influence <= 1) || clip.fadeIn < 0 || clip.fadeOut < 0) return false;
        int count = rigClipFrames(*rig.actions[clip.action], clip);
        if (count == 0 || clip.start > 100000 - count || clip.fadeIn >= count || clip.fadeOut >= count) return false;
    }
    return true;
}

bool validRig(const Rig& rig, size_t vertices) {
    if (rig.joints.empty() || rig.joints.size() > MAX_RIG_JOINTS || vertices > MAX_SKIN_VERTICES ||
        (!rig.weights.empty() && rig.weights.size() != vertices)) return false;
    for (size_t i = 0; i < rig.joints.size(); ++i) {
        const auto& joint = rig.joints[i];
        if (joint.parent < -1 || joint.parent >= (int)i || joint.name.empty() || joint.name.size() > 120 ||
            !finiteV(joint.head) || !finiteV(joint.tail) || length(joint.tail - joint.head) < .0001f ||
            !validKey(joint.pose) || joint.keys.size() > MAX_JOINT_KEYS) return false;
        int last = -1;
        for (const auto& key : joint.keys) {
            if (!validKey(key) || key.frame <= last) return false;
            last = key.frame;
        }
    }
    for (const auto& v : rig.weights) {
        float total = 0;
        for (size_t i = 0; i < v.weight.size(); ++i) {
            if (v.joint[i] >= rig.joints.size() || !(v.weight[i] >= 0 && v.weight[i] <= 1)) return false;
            if (v.weight[i] > 0) {
                for (size_t j = 0; j < i; ++j)
                    if (v.weight[j] > 0 && v.joint[j] == v.joint[i]) return false;
            }
            total += v.weight[i];
        }
        if (total > 0 && std::fabs(total - 1.f) > .001f) return false;
    }
    if (rig.ik.size() > rig.joints.size() / 2) return false;
    std::array<bool, MAX_RIG_JOINTS> driven{};
    for (const auto& chain : rig.ik) {
        if (chain.end < 0 || chain.end >= (int)rig.joints.size() || !validGoal(chain.settings) ||
            chain.keys.size() > MAX_JOINT_KEYS) return false;
        const Joint& end = rig.joints[chain.end];
        int start = end.parent;
        if (start < 0 || driven[start] || driven[chain.end] ||
            length(end.head - rig.joints[start].head) < .0001f) return false;
        driven[start] = driven[chain.end] = true;
        int last = -1;
        for (const auto& key : chain.keys) {
            if (!validGoal(key) || key.frame <= last) return false;
            last = key.frame;
        }
    }
    return validRigMotion(rig);
}

std::shared_ptr<const RigAction> captureRigAction(const Rig& rig, int first, int last, const std::string& name) {
    if (first < 0 || last < first || last >= 100000 || last - first >= MAX_ACTION_FRAMES ||
        rig.joints.empty() || rig.actions.size() >= MAX_RIG_ACTIONS || name.empty() || name.size() > 120) return {};
    auto action = std::make_shared<RigAction>();
    action->name = name;
    action->frames = last - first + 1;
    for (size_t i = 0; i < rig.joints.size(); ++i) action->joints.push_back({(int)i, {}});
    for (const auto& chain : rig.ik) action->ik.push_back({chain.end, {}});
    for (int i = 0; i < action->frames; ++i) {
        auto sample = sampleRigFrame(rig, first + i);
        for (size_t j = 0; j < sample.joints.size(); ++j) {
            auto& key = sample.joints[j];
            key.frame = i;
            key.ease = Ease::Linear;
            action->joints[j].keys.push_back(key);
        }
        for (size_t j = 0; j < sample.controls.size(); ++j) {
            auto& key = sample.controls[j];
            key.frame = i;
            key.ease = Ease::Linear;
            action->ik[j].keys.push_back(key);
        }
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    for (auto& track : action->joints) {
        const auto& start = track.keys.front();
        if (std::all_of(track.keys.begin(), track.keys.end(), [&](const Key& key) {
            return same(start.pos, key.pos) && same(start.rot, key.rot) && same(start.scl, key.scl);
        })) track.keys.resize(1);
    }
    for (auto& track : action->ik) {
        const auto& start = track.keys.front();
        if (std::all_of(track.keys.begin(), track.keys.end(), [&](const IKKey& key) {
            return same(start.target, key.target) && same(start.pole, key.pole) && start.influence == key.influence;
        })) track.keys.resize(1);
    }
    if (countSamples(rig) + countSamples(*action) > MAX_ACTION_SAMPLES) return {};
    return action;
}

std::shared_ptr<const RigAction> transferRigAction(const Rig& source, int action, const Rig& destination) {
    if (action < 0 || action >= (int)source.actions.size() || destination.actions.size() >= MAX_RIG_ACTIONS) return {};
    std::map<std::string, int> byName, sourceNames;
    for (size_t i = 0; i < destination.joints.size(); ++i)
        if (!byName.emplace(destination.joints[i].name, (int)i).second) return {};
    for (size_t i = 0; i < source.joints.size(); ++i)
        if (!sourceNames.emplace(source.joints[i].name, (int)i).second) return {};
    auto match = [&](int index) {
        const Joint& original = source.joints[index];
        auto found = byName.find(original.name);
        if (found == byName.end()) return -1;
        int parent = destination.joints[found->second].parent;
        if (original.parent < 0 && parent < 0) return found->second;
        if (original.parent < 0 || parent < 0) return -1;
        return source.joints[original.parent].name == destination.joints[parent].name ? found->second : -1;
    };
    auto transferred = std::make_shared<RigAction>(*source.actions[action]);
    for (auto& track : transferred->joints) {
        int index = match(track.joint);
        if (index < 0) return {};
        const auto& a = source.joints[track.joint];
        const auto& b = destination.joints[index];
        float ratio = length(b.tail - b.head) / length(a.tail - a.head);
        for (auto& key : track.keys) key.pos *= ratio;
        track.joint = index;
    }
    for (auto& track : transferred->ik) {
        int index = match(track.end);
        if (index < 0 || std::none_of(destination.ik.begin(), destination.ik.end(),
            [&](const RigIK& ik) { return ik.end == index; })) return {};
        const auto& sourceEnd = source.joints[track.end];
        const auto& end = destination.joints[index];
        const auto& sourceRoot = source.joints[sourceEnd.parent];
        const auto& root = destination.joints[end.parent];
        if (match(sourceEnd.parent) != end.parent) return {};
        V3 a = sourceEnd.head - sourceRoot.head, b = end.head - root.head;
        float ratio = (length(b) + length(end.tail - end.head)) /
                      (length(a) + length(sourceEnd.tail - sourceEnd.head));
        M3 basis = restBasis(b) * restBasis(a).transpose();
        for (auto& key : track.keys) {
            key.target = root.head + basis * (key.target - sourceRoot.head) * ratio;
            key.pole = root.head + basis * (key.pole - sourceRoot.head) * ratio;
        }
        track.end = index;
    }
    Rig check;
    check.joints = destination.joints;
    check.ik = destination.ik;
    check.actions = destination.actions;
    check.actions.push_back(transferred);
    if (!validRigMotion(check)) return {};
    return transferred;
}

void removeRigAction(Rig& rig, int action) {
    rig.clips.erase(std::remove_if(rig.clips.begin(), rig.clips.end(),
        [&](const RigClip& clip) { return clip.action == action; }), rig.clips.end());
    for (auto& clip : rig.clips)
        if (clip.action > action) --clip.action;
    rig.actions.erase(rig.actions.begin() + action);
}

void remapRigActionTracks(Rig& rig, int removedJoint, int removedIK) {
    for (int i = (int)rig.actions.size() - 1; i >= 0; --i) {
        auto copy = std::make_shared<RigAction>(*rig.actions[i]);
        if (removedJoint >= 0) {
            auto& tracks = copy->joints;
            tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                [&](const JointActionTrack& track) { return track.joint == removedJoint; }), tracks.end());
            for (auto& track : tracks)
                if (track.joint > removedJoint) --track.joint;
        }
        auto& controls = copy->ik;
        controls.erase(std::remove_if(controls.begin(), controls.end(),
            [&](const IKActionTrack& track) { return track.end == removedIK || track.end == removedJoint; }), controls.end());
        if (removedJoint >= 0)
            for (auto& track : controls)
                if (track.end > removedJoint) --track.end;
        if (copy->joints.empty() && controls.empty()) removeRigAction(rig, i);
        else rig.actions[i] = std::move(copy);
    }
}

bool bakeRigClips(Rig& rig, int frames) {
    if (rig.clips.empty() || frames < 1 || frames > MAX_ACTION_FRAMES) return false;
    std::array<bool, MAX_RIG_JOINTS> joints{}, controls{};
    for (const auto& clip : rig.clips) {
        const auto& action = *rig.actions[clip.action];
        if (clip.start + rigClipFrames(action, clip) > frames) return false;
        for (const auto& track : action.joints) joints[track.joint] = true;
        for (const auto& track : action.ik) controls[track.end] = true;
    }
    Rig edited = rig;
    for (size_t i = 0; i < edited.joints.size(); ++i)
        if (joints[i]) edited.joints[i].keys.clear();
    for (auto& chain : edited.ik)
        if (controls[chain.end]) chain.keys.clear();
    for (int frame = 0; frame < frames; ++frame) {
        auto sample = sampleRigFrame(rig, frame);
        for (size_t i = 0; i < edited.joints.size(); ++i) {
            if (!joints[i]) continue;
            Key key = sample.joints[i];
            key.frame = frame;
            key.ease = Ease::Linear;
            if (!validKey(key)) return false;
            edited.joints[i].keys.push_back(key);
        }
        for (size_t i = 0; i < edited.ik.size(); ++i) {
            if (!controls[edited.ik[i].end]) continue;
            IKKey key = sample.controls[i];
            key.frame = frame;
            key.ease = Ease::Linear;
            if (!validGoal(key)) return false;
            edited.ik[i].keys.push_back(key);
        }
    }
    auto appendFuture = [frames](auto& keys, const auto& original) {
        auto first = std::lower_bound(original.begin(), original.end(), frames,
            [](const auto& key, int frame) { return key.frame < frame; });
        keys.insert(keys.end(), first, original.end());
        return keys.size() <= MAX_JOINT_KEYS;
    };
    for (size_t i = 0; i < edited.joints.size(); ++i)
        if (joints[i] && !appendFuture(edited.joints[i].keys, rig.joints[i].keys)) return false;
    for (size_t i = 0; i < edited.ik.size(); ++i)
        if (controls[edited.ik[i].end] && !appendFuture(edited.ik[i].keys, rig.ik[i].keys)) return false;
    edited.clips.clear();
    rig = std::move(edited);
    return true;
}

std::vector<PosedJoint> poseRig(const Rig& rig, int frame) {
    RigFrame sample = sampleRigFrame(rig, frame);
    std::vector<PosedJoint> posed;
    posed.reserve(rig.joints.size());
    for (const auto& joint : rig.joints) {
        const Key& key = sample.joints[posed.size()];
        V3 head = joint.head;
        M3 rest = restBasis(joint.tail - joint.head);
        M3 basis = rest;
        if (joint.parent >= 0) {
            const auto& parent = posed[joint.parent];
            M3 transform = parent.basis * parent.inverseBind;
            head = parent.head + transform * (head - rig.joints[joint.parent].head);
            basis = transform * basis;
        }
        head += basis * key.pos;
        basis = basis * rotY(rad(key.rot.y)) * rotX(rad(key.rot.x)) * rotZ(rad(key.rot.z)) * scaleM(key.scl);
        V3 tail = head + basis.col(1) * length(joint.tail - joint.head);
        if (!finiteV(head) || !finiteV(tail) || !finiteV(basis.col(0)) ||
            !finiteV(basis.col(1)) || !finiteV(basis.col(2))) return {};
        posed.push_back({head, tail, basis, rest.transpose()});
    }
    if (!rig.ik.empty()) {
        std::vector<const RigIK*> chains;
        for (const auto& chain : rig.ik) chains.push_back(&chain);
        std::sort(chains.begin(), chains.end(), [&](const RigIK* a, const RigIK* b) {
            return rig.joints[a->end].parent < rig.joints[b->end].parent;
        });
        for (const RigIK* chain : chains) applyIK(rig, posed, *chain, sample.controls[chain - rig.ik.data()]);
    }
    return posed;
}

bool bindRig(Rig& rig, const Mesh& mesh) {
    const auto& vertices = mesh.skinVertices();
    if (vertices.empty() || vertices.size() > MAX_SKIN_VERTICES || rig.joints.empty() ||
        rig.joints.size() > MAX_RIG_JOINTS) return false;
    std::vector<V3> directions;
    std::vector<float> invLengths;
    for (const auto& joint : rig.joints) {
        V3 delta = joint.tail - joint.head;
        float square = dot(delta, delta);
        if (!(square >= 1e-8f)) return false;
        directions.push_back(delta);
        invLengths.push_back(1.f / square);
    }
    float epsilon = std::max(.00001f, length(mesh.bmax - mesh.bmin) * .02f);
    std::vector<SkinWeights> weights(vertices.size());
    for (size_t v = 0; v < vertices.size(); ++v) {
        std::array<float, 4> distances{1e30f, 1e30f, 1e30f, 1e30f};
        auto& vertex = weights[v];
        for (size_t j = 0; j < rig.joints.size(); ++j) {
            V3 offset = vertices[v] - rig.joints[j].head;
            V3 delta = directions[j];
            V3 near = offset - delta * clampf(dot(offset, delta) * invLengths[j], 0, 1);
            float square = dot(near, near);
            for (size_t slot = 0; slot < distances.size(); ++slot) {
                if (square >= distances[slot]) continue;
                for (size_t s = distances.size() - 1; s > slot; --s) {
                    distances[s] = distances[s - 1];
                    vertex.joint[s] = vertex.joint[s - 1];
                }
                distances[slot] = square;
                vertex.joint[slot] = (uint16_t)j;
                break;
            }
        }
        float total = 0;
        size_t count = std::min(distances.size(), rig.joints.size());
        for (size_t i = 0; i < count; ++i) {
            vertex.weight[i] = 1.f / (distances[i] + epsilon * epsilon);
            total += vertex.weight[i];
        }
        if (!(total > 0)) return false;
        for (auto& weight : vertex.weight) weight /= total;
    }
    rig.weights = std::move(weights);
    return true;
}

bool setSkinWeight(SkinWeights& weights, int joint, float weight, size_t jointCount) {
    if (joint < 0 || (size_t)joint >= jointCount || !(weight >= 0 && weight <= 1)) return false;
    SkinWeights result;
    size_t others = 0;
    for (size_t i = 0; i < weights.weight.size(); ++i) {
        if (weights.weight[i] <= 0 || weights.joint[i] == joint) continue;
        result.joint[others] = weights.joint[i];
        result.weight[others++] = weights.weight[i];
    }
    if (weight > 0 && others == result.weight.size()) {
        size_t weakest = (size_t)(std::min_element(result.weight.begin(), result.weight.end()) - result.weight.begin());
        --others;
        result.joint[weakest] = result.joint[others];
        result.weight[weakest] = result.weight[others];
        result.weight[others] = 0;
    }
    if (others == 0 && weight > 0 && weight < 1) {
        if (jointCount < 2) return false;
        result.joint[0] = (uint16_t)(joint == 0 ? 1 : 0);
        result.weight[0] = 1;
        others = 1;
    }
    float total = 0;
    for (float item : result.weight) total += item;
    if (total > 0) {
        float scale = (1 - weight) / total;
        for (float& item : result.weight) item *= scale;
    }
    if (weight > 0) {
        if (others >= result.weight.size()) return false;
        result.joint[others] = (uint16_t)joint;
        result.weight[others] = weight;
    }
    weights = result;
    return true;
}

std::shared_ptr<const Mesh> deformRig(const Rig& rig, const Mesh& mesh, int frame) {
    if (rig.weights.empty() || rig.weights.size() != mesh.skinVertices().size()) return {};
    auto posed = poseRig(rig, frame);
    if (posed.size() != rig.joints.size()) return {};
    std::vector<M3> transforms, normals;
    for (auto& joint : posed) {
        transforms.push_back(joint.basis * joint.inverseBind);
        normals.push_back(transforms.back().inverse().transpose());
    }
    auto result = std::make_shared<Mesh>();
    result->tri = mesh.tri;
    result->uv = mesh.uv;
    result->v.resize(mesh.v.size());
    bool sourceNormals = mesh.n.size() == mesh.v.size();
    if (sourceNormals) result->n.resize(mesh.v.size());
    for (size_t v = 0; v < mesh.v.size(); ++v) {
        const auto& skin = rig.weights[mesh.skinVertex(v)];
        V3 p, n;
        float total = 0;
        for (size_t i = 0; i < skin.weight.size(); ++i) {
            float weight = skin.weight[i];
            if (weight <= 0) continue;
            uint16_t j = skin.joint[i];
            p += (posed[j].head + transforms[j] * (mesh.v[v] - rig.joints[j].head)) * weight;
            if (sourceNormals) n += normals[j] * mesh.n[v] * weight;
            total += weight;
        }
        result->v[v] = total > 0 ? p / total : mesh.v[v];
        if (!finiteV(result->v[v])) return {};
        if (sourceNormals) result->n[v] = length(n) > 1e-8f ? normalize(n) : mesh.n[v];
    }
    result->build();
    return result;
}

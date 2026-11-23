#include "scene.h"

namespace {

using T = TexType;

Material M(const char* cat, const char* n, T t, V3 a, V3 b, float diff, float amb, float spec, float shin, float refl, float trans,
           float ior, float bump, float scale, float metal = 0, float emission = 0, float blur = 0, TexMap map = TexMap::Object) {
    Material m;
    m.category = cat; m.name = n; m.tex = t; m.color1 = a; m.color2 = b; m.diffuse = diff; m.ambient = amb; m.specular = spec;
    m.shininess = shin; m.reflect = refl; m.transparency = trans; m.ior = ior; m.bump = bump; m.texScale = scale; m.metal = metal;
    m.emission = emission; m.blur = blur; m.map = map;
    return m;
}

struct SkyB {
    Sky s;
    SkyB(const char* cat, const char* n, V3 zen, V3 hor, V3 sun, float alt, float az) {
        s.category = cat; s.name = n; s.zenith = zen; s.horizon = hor; s.sunColor = sun; s.sunAlt = alt; s.sunAz = az;
    }
    SkyB& haze(float h, V3 c) { s.haze = h; s.hazeColor = c; return *this; }
    SkyB& fog(float d, float h, V3 c) { s.fog = d; s.fogHeight = h; s.fogColor = c; return *this; }
    SkyB& clouds(float cu, float st, V3 c, float scale = 1) { s.clouds = cu; s.stratus = st; s.cloudColor = c; s.cloudScale = scale; return *this; }
    SkyB& amb(float a, V3 c) { s.ambient = a; s.ambientColor = c; return *this; }
    SkyB& stars(float v) { s.stars = v; return *this; }
    SkyB& moon(float alt, float az, float phase, float size = 1.6f) { s.moon = true; s.moonAlt = alt; s.moonAz = az; s.moonPhase = phase; s.moonSize = size; return *this; }
    SkyB& rainbow(float v) { s.rainbow = v; return *this; }
    SkyB& mode(int m) { s.mode = m; return *this; }
    SkyB& noShadows() { s.shadows = false; return *this; }
    SkyB& sunSize(float v) { s.sunSize = v; return *this; }
    SkyB& hideSun() { s.sunVisible = false; return *this; }
};

}  // namespace

const std::vector<Material>& materialPresets() {
    static const std::vector<Material> p = {
        // Basic
        M("Basic", "Default Gray", T::None, {.72f, .72f, .72f}, {.3f, .3f, .3f}, .85f, .3f, .25f, 30, 0, 0, 1, 0, 1),
        M("Basic", "Matte White", T::None, {.92f, .92f, .9f}, {0, 0, 0}, .9f, .35f, .05f, 10, 0, 0, 1, 0, 1),
        M("Basic", "Red Plastic", T::None, {.8f, .08f, .06f}, {0, 0, 0}, .85f, .3f, .6f, 60, .05f, 0, 1, 0, 1),
        M("Basic", "Blue Plastic", T::None, {.1f, .25f, .8f}, {0, 0, 0}, .85f, .3f, .6f, 60, .05f, 0, 1, 0, 1),
        M("Basic", "Green Plastic", T::None, {.12f, .62f, .2f}, {0, 0, 0}, .85f, .3f, .6f, 60, .05f, 0, 1, 0, 1),
        M("Basic", "Yellow Plastic", T::None, {.95f, .78f, .1f}, {0, 0, 0}, .85f, .3f, .6f, 60, .05f, 0, 1, 0, 1),
        M("Basic", "Black Gloss", T::None, {.04f, .04f, .05f}, {0, 0, 0}, .8f, .2f, .9f, 120, .2f, 0, 1, 0, 1),
        M("Basic", "Checker", T::Checker, {.9f, .9f, .9f}, {.12f, .12f, .12f}, .85f, .3f, .2f, 30, 0, 0, 1, 0, 1),
        M("Basic", "Sunset Gradient", T::Gradient, {1, .85f, .3f}, {.6f, .1f, .35f}, .85f, .4f, .2f, 30, 0, 0, 1, 0, 1),
        // Metals
        M("Metals", "Chrome", T::None, {.9f, .9f, .95f}, {0, 0, 0}, .08f, .1f, 1, 150, .9f, 0, 1, 0, 1),
        M("Metals", "Gold", T::None, {1, .76f, .3f}, {0, 0, 0}, .3f, .15f, .9f, 90, .65f, 0, 1, 0, 1, 1),
        M("Metals", "Silver", T::None, {.85f, .86f, .88f}, {0, 0, 0}, .25f, .15f, .9f, 100, .7f, 0, 1, 0, 1, 1),
        M("Metals", "Copper", T::None, {.95f, .5f, .3f}, {0, 0, 0}, .35f, .15f, .8f, 70, .55f, 0, 1, 0, 1, 1),
        M("Metals", "Brass", T::None, {.85f, .7f, .35f}, {0, 0, 0}, .4f, .15f, .8f, 60, .45f, 0, 1, 0, 1, 1),
        M("Metals", "Bronze", T::Noise, {.7f, .45f, .22f}, {.5f, .32f, .15f}, .45f, .2f, .7f, 50, .35f, 0, 1, .1f, 2, 1),
        M("Metals", "Brushed Steel", T::None, {.7f, .72f, .75f}, {0, 0, 0}, .35f, .2f, .7f, 40, .5f, 0, 1, 0, 1, 1, 0, .35f),
        M("Metals", "Rusty Iron", T::Rust, {.45f, .45f, .48f}, {.55f, .25f, .1f}, .8f, .25f, .3f, 30, .15f, 0, 1, .5f, 1.5f),
        // Glass
        M("Glass", "Glass", T::None, {.95f, .98f, 1}, {0, 0, 0}, .02f, .05f, 1, 200, .05f, .92f, 1.5f, 0, 1),
        M("Glass", "Blue Tinted Glass", T::None, {.55f, .7f, 1}, {0, 0, 0}, .04f, .05f, 1, 200, .05f, .85f, 1.5f, 0, 1),
        M("Glass", "Bottle Green", T::None, {.35f, .75f, .35f}, {0, 0, 0}, .05f, .05f, 1, 200, .05f, .8f, 1.52f, 0, 1),
        M("Glass", "Frosted Glass", T::None, {.92f, .95f, .98f}, {0, 0, 0}, .25f, .2f, .6f, 40, .1f, .7f, 1.5f, .3f, 4, 0, 0, .5f),
        M("Glass", "Crystal", T::None, {1, 1, 1}, {0, 0, 0}, .01f, .05f, 1, 250, .1f, .95f, 2.0f, 0, 1),
        M("Glass", "Soap Bubble", T::Marble, {.95f, .85f, 1}, {.7f, 1, .9f}, .02f, .05f, 1, 200, .15f, .95f, 1.04f, 0, 2),
        // Rocks
        M("Rocks", "Granite", T::Granite, {.62f, .6f, .57f}, {.27f, .26f, .24f}, .85f, .3f, .3f, 40, 0, 0, 1, .3f, 1.5f),
        M("Rocks", "Marble", T::Marble, {.95f, .94f, .92f}, {.3f, .32f, .38f}, .8f, .3f, .7f, 80, .12f, 0, 1, 0, .8f),
        M("Rocks", "Sandstone", T::Strata, {.88f, .72f, .5f}, {.7f, .52f, .34f}, .9f, .3f, .05f, 10, 0, 0, 1, .4f, 2),
        M("Rocks", "Slate", T::Noise, {.32f, .34f, .37f}, {.2f, .21f, .23f}, .8f, .3f, .3f, 30, 0, 0, 1, .35f, 2.5f),
        M("Rocks", "Volcanic Rock", T::Granite, {.25f, .22f, .21f}, {.08f, .07f, .07f}, .85f, .25f, .2f, 20, 0, 0, 1, .7f, 2),
        M("Rocks", "Pebbles", T::Spots, {.6f, .56f, .5f}, {.38f, .34f, .3f}, .85f, .3f, .2f, 20, 0, 0, 1, .6f, 3),
        M("Rocks", "Basalt", T::Noise, {.2f, .2f, .22f}, {.12f, .12f, .13f}, .85f, .25f, .25f, 25, 0, 0, 1, .5f, 4),
        // Terrains
        M("Terrains", "Snowy Peaks", T::Altitude, {.96f, .97f, 1}, {.45f, .38f, .32f}, .9f, .3f, .1f, 10, 0, 0, 1, .6f, 3),
        M("Terrains", "Canyon Strata", T::Strata, {.86f, .55f, .34f}, {.58f, .28f, .17f}, .9f, .3f, .05f, 10, 0, 0, 1, .5f, 1),
        M("Terrains", "Desert Sand", T::Noise, {.93f, .8f, .56f}, {.78f, .62f, .4f}, .9f, .35f, .05f, 10, 0, 0, 1, .25f, .6f),
        M("Terrains", "Grass", T::Noise, {.32f, .5f, .16f}, {.18f, .32f, .09f}, .9f, .3f, .05f, 10, 0, 0, 1, .3f, .8f),
        M("Terrains", "Earth", T::Noise, {.5f, .43f, .34f}, {.33f, .28f, .22f}, .9f, .3f, .05f, 10, 0, 0, 1, .2f, .4f),
        M("Terrains", "Alpine Meadow", T::Altitude, {.9f, .92f, .95f}, {.35f, .4f, .25f}, .9f, .3f, .05f, 10, 0, 0, 1, .5f, 2),
        M("Terrains", "Red Mesa", T::Strata, {.8f, .38f, .2f}, {.5f, .2f, .1f}, .9f, .3f, .05f, 10, 0, 0, 1, .5f, 1.5f),
        M("Terrains", "Tundra", T::Altitude, {.85f, .87f, .9f}, {.4f, .4f, .38f}, .9f, .3f, .05f, 10, 0, 0, 1, .4f, 2),
        M("Terrains", "Lunar Dust", T::Spots, {.62f, .62f, .6f}, {.42f, .42f, .41f}, .9f, .25f, .02f, 10, 0, 0, 1, .7f, 1.2f),
        // Waters
        M("Waters", "Water", T::Waves, {.12f, .32f, .42f}, {0, 0, 0}, .25f, .3f, .9f, 120, .35f, .55f, 1.33f, .5f, .6f),
        M("Waters", "Tropical Lagoon", T::Waves, {.1f, .6f, .6f}, {0, 0, 0}, .3f, .35f, .9f, 120, .25f, .7f, 1.33f, .35f, .5f),
        M("Waters", "Murky Pond", T::Waves, {.2f, .25f, .12f}, {0, 0, 0}, .5f, .3f, .6f, 80, .2f, .2f, 1.33f, .2f, .8f),
        M("Waters", "Deep Ocean", T::Waves, {.02f, .1f, .2f}, {0, 0, 0}, .2f, .2f, 1, 150, .5f, .3f, 1.33f, .8f, .3f),
        M("Waters", "Ice", T::Noise, {.8f, .9f, 1}, {.65f, .8f, .95f}, .2f, .3f, .9f, 120, .15f, .6f, 1.31f, .2f, 2),
        M("Waters", "Molten Lava", T::Lava, {1, .75f, .2f}, {.25f, .03f, .01f}, .9f, .2f, .2f, 20, 0, 0, 1, .6f, 1, 0, 1.2f),
        // Clouds
        M("Clouds", "Clouds", T::Clouds, {1, 1, 1}, {.72f, .75f, .8f}, .8f, .7f, 0, 10, 0, 0, 1, 0, .035f),
        M("Clouds", "Storm Clouds", T::Clouds, {.55f, .57f, .6f}, {.25f, .26f, .3f}, .8f, .5f, 0, 10, 0, 0, 1, 0, .03f),
        M("Clouds", "Wispy Clouds", T::Clouds, {1, 1, 1}, {.85f, .88f, .92f}, .8f, .8f, 0, 10, 0, 0, 1, 0, .08f),
        M("Clouds", "Sunset Clouds", T::Clouds, {1, .7f, .45f}, {.65f, .35f, .4f}, .8f, .7f, 0, 10, 0, 0, 1, 0, .04f),
        // Woods
        M("Woods", "Wood", T::Wood, {.78f, .52f, .27f}, {.45f, .25f, .1f}, .85f, .3f, .3f, 30, 0, 0, 1, .1f, 1),
        M("Woods", "Dark Walnut", T::Wood, {.42f, .26f, .14f}, {.2f, .11f, .05f}, .85f, .3f, .5f, 50, .05f, 0, 1, .1f, 1.5f),
        M("Woods", "Pine", T::Wood, {.92f, .75f, .5f}, {.7f, .5f, .28f}, .85f, .3f, .2f, 30, 0, 0, 1, .1f, .8f),
        M("Woods", "Bark", T::Strata, {.4f, .3f, .22f}, {.22f, .16f, .11f}, .9f, .3f, .05f, 10, 0, 0, 1, 1, 6),
        M("Woods", "Leaves", T::Noise, {.3f, .55f, .15f}, {.12f, .3f, .06f}, .9f, .35f, .15f, 20, 0, 0, 1, .2f, 6),
        M("Woods", "Autumn Leaves", T::Noise, {.95f, .55f, .1f}, {.65f, .15f, .05f}, .9f, .35f, .15f, 20, 0, 0, 1, .2f, 6),
        // Building
        M("Building", "Brick Wall", T::Bricks, {.65f, .25f, .16f}, {.75f, .72f, .66f}, .9f, .3f, .1f, 10, 0, 0, 1, .5f, 2),
        M("Building", "Concrete", T::Noise, {.66f, .65f, .62f}, {.52f, .51f, .49f}, .9f, .3f, .1f, 10, 0, 0, 1, .25f, 3),
        M("Building", "Cobblestone", T::Spots, {.5f, .48f, .45f}, {.28f, .27f, .25f}, .85f, .3f, .15f, 15, 0, 0, 1, .8f, 2),
        M("Building", "Terracotta", T::Noise, {.78f, .4f, .25f}, {.68f, .33f, .2f}, .9f, .3f, .15f, 20, 0, 0, 1, .15f, 3),
        // Fantasy
        M("Fantasy", "Neon Glow", T::None, {.2f, 1, .9f}, {0, 0, 0}, .5f, .3f, .5f, 60, 0, 0, 1, 0, 1, 0, 1.5f),
        M("Fantasy", "Plasma", T::Lava, {.6f, .8f, 1}, {.1f, .02f, .3f}, .8f, .3f, .3f, 40, 0, 0, 1, .3f, 1.5f, 0, 1.2f),
        M("Fantasy", "Alien Slime", T::Spots, {.5f, .95f, .2f}, {.15f, .4f, .05f}, .6f, .4f, .9f, 90, .15f, .2f, 1.2f, .6f, 2, 0, .25f),
        M("Fantasy", "Gas Giant", T::Rings, {.9f, .75f, .55f}, {.6f, .35f, .2f}, .9f, .3f, .05f, 10, 0, 0, 1, 0, 1),
        M("Fantasy", "Psychedelic", T::Marble, {1, .1f, .8f}, {.1f, .9f, 1}, .85f, .4f, .5f, 50, .05f, 0, 1, 0, 2),
        M("Fantasy", "Hologram", T::Rings, {.4f, .9f, 1}, {.05f, .2f, .4f}, .3f, .3f, .8f, 80, .1f, .6f, 1.02f, 0, 6, 0, .5f),
        M("Fantasy", "Leopard", T::Spots, {.9f, .7f, .35f}, {.15f, .1f, .05f}, .9f, .35f, .1f, 15, 0, 0, 1, 0, 3),
    };
    return p;
}

Material findMaterial(const std::string& name) {
    for (auto& m : materialPresets())
        if (m.name == name) return m;
    return Material();
}

const std::vector<Sky>& skyPresets() {
    static const std::vector<Sky> p = [] {
        V3 white(1.f);
        std::vector<SkyB> b = {
            SkyB("Day", "Clear Day", {.2f, .4f, .85f}, {.72f, .8f, .95f}, {1, .97f, .9f}, 45, 135).haze(.15f, {.75f, .82f, .92f}).clouds(.25f, 0, white).amb(.5f, {.55f, .65f, .85f}),
            SkyB("Day", "Bright Noon", {.12f, .35f, .9f}, {.62f, .78f, .98f}, {1, 1, .95f}, 75, 160).haze(.08f, {.7f, .8f, .95f}).clouds(.15f, 0, white).amb(.55f, {.55f, .65f, .9f}),
            SkyB("Day", "Hazy Afternoon", {.4f, .55f, .8f}, {.85f, .85f, .82f}, {1, .93f, .8f}, 35, 220).haze(.5f, {.85f, .84f, .8f}).clouds(.2f, .2f, white).amb(.55f, {.65f, .68f, .75f}),
            SkyB("Day", "Fair Weather", {.18f, .42f, .86f}, {.7f, .82f, .96f}, {1, .97f, .92f}, 50, 120).haze(.12f, {.75f, .82f, .92f}).clouds(.5f, 0, white, 1.4f).amb(.55f, {.58f, .66f, .85f}),
            SkyB("Day", "High Cirrus", {.2f, .45f, .85f}, {.75f, .85f, .95f}, {1, .97f, .9f}, 55, 100).haze(.1f, {.78f, .85f, .94f}).clouds(0, .6f, white).amb(.5f, {.58f, .66f, .86f}),
            SkyB("Day", "Tropical", {.05f, .45f, .85f}, {.6f, .88f, .95f}, {1, .98f, .88f}, 65, 140).haze(.15f, {.7f, .9f, .95f}).clouds(.35f, 0, white, 1.2f).amb(.6f, {.55f, .75f, .85f}),
            SkyB("Dawn & Dusk", "Sunset", {.15f, .2f, .45f}, {1, .55f, .25f}, {1, .6f, .3f}, 6, 200).haze(.35f, {.95f, .6f, .4f}).clouds(.35f, 0, {1, .7f, .5f}).amb(.35f, {.6f, .45f, .5f}),
            SkyB("Dawn & Dusk", "Golden Hour", {.3f, .45f, .75f}, {1, .8f, .5f}, {1, .8f, .5f}, 14, 250).haze(.3f, {1, .85f, .6f}).clouds(.2f, .1f, {1, .9f, .7f}).amb(.45f, {.7f, .62f, .55f}),
            SkyB("Dawn & Dusk", "Dawn", {.25f, .3f, .55f}, {.95f, .7f, .65f}, {1, .75f, .6f}, 4, 90).haze(.3f, {.9f, .75f, .72f}).clouds(.15f, .3f, {1, .8f, .75f}).amb(.35f, {.6f, .55f, .65f}),
            SkyB("Dawn & Dusk", "Twilight", {.05f, .07f, .25f}, {.55f, .35f, .5f}, {.8f, .5f, .5f}, -2, 270).haze(.2f, {.4f, .3f, .45f}).clouds(.1f, .2f, {.6f, .4f, .5f}).amb(.25f, {.35f, .3f, .5f}).stars(.4f),
            SkyB("Dawn & Dusk", "Misty Morning", {.5f, .6f, .75f}, {.85f, .85f, .85f}, {1, .9f, .75f}, 15, 110).haze(.4f, {.85f, .85f, .87f}).fog(.5f, 4, {.85f, .85f, .88f}).clouds(.2f, 0, {1, .95f, .9f}).amb(.55f, {.7f, .72f, .78f}),
            SkyB("Weather", "Overcast", {.55f, .58f, .62f}, {.75f, .77f, .8f}, {.6f, .6f, .6f}, 60, 135).haze(.3f, {.75f, .77f, .8f}).clouds(.95f, .3f, {.85f, .86f, .88f}).amb(.8f, {.7f, .72f, .75f}).noShadows().hideSun(),
            SkyB("Weather", "Stormy", {.22f, .24f, .28f}, {.45f, .47f, .5f}, {.7f, .7f, .7f}, 30, 200).haze(.35f, {.4f, .42f, .45f}).clouds(.85f, .4f, {.45f, .47f, .52f}, 1.3f).amb(.55f, {.45f, .47f, .52f}).hideSun(),
            SkyB("Weather", "After the Rain", {.25f, .45f, .75f}, {.75f, .8f, .85f}, {1, .95f, .85f}, 25, 120).haze(.2f, {.8f, .83f, .88f}).clouds(.55f, 0, {.9f, .92f, .95f}).amb(.55f, {.6f, .66f, .8f}).rainbow(.8f),
            SkyB("Weather", "Smog", {.55f, .52f, .45f}, {.75f, .68f, .55f}, {1, .8f, .55f}, 30, 210).haze(.7f, {.72f, .65f, .52f}).clouds(.1f, .3f, {.8f, .75f, .65f}).amb(.5f, {.6f, .55f, .5f}),
            SkyB("Weather", "Desert Heat", {.3f, .5f, .85f}, {.95f, .88f, .72f}, {1, .95f, .82f}, 60, 180).haze(.45f, {.95f, .88f, .75f}).clouds(.05f, .1f, white).amb(.6f, {.75f, .72f, .7f}),
            SkyB("Weather", "Arctic", {.45f, .6f, .85f}, {.88f, .92f, .98f}, {1, .98f, .95f}, 12, 160).haze(.3f, {.9f, .93f, .98f}).fog(.25f, 2, {.9f, .93f, .97f}).clouds(.2f, .2f, white).amb(.7f, {.75f, .82f, .95f}),
            SkyB("Night", "Night", {.01f, .02f, .08f}, {.08f, .1f, .25f}, {.5f, .55f, .8f}, 35, 100).haze(.1f, {.1f, .12f, .25f}).clouds(.1f, 0, {.3f, .32f, .4f}).amb(.3f, {.15f, .2f, .4f}).stars(1).hideSun().moon(35, 100, .5f),
            SkyB("Night", "Moonrise", {.02f, .03f, .12f}, {.2f, .18f, .3f}, {.55f, .55f, .7f}, 8, 80).haze(.15f, {.2f, .18f, .3f}).clouds(.15f, .2f, {.3f, .3f, .4f}).amb(.25f, {.2f, .2f, .4f}).stars(.8f).hideSun().moon(8, 80, .45f, 3),
            SkyB("Night", "Crescent Night", {0, .01f, .05f}, {.05f, .07f, .18f}, {.3f, .32f, .45f}, 25, 240).haze(.08f, {.05f, .07f, .18f}).amb(.2f, {.1f, .12f, .3f}).stars(1).hideSun().moon(25, 240, .15f, 2),
            SkyB("Fantasy", "Alien", {.1f, .3f, .15f}, {.8f, .9f, .4f}, {.9f, 1, .6f}, 25, 160).haze(.3f, {.6f, .8f, .3f}).clouds(.4f, 0, {.9f, 1, .7f}).amb(.45f, {.5f, .7f, .4f}).moon(40, 220, .7f, 6),
            SkyB("Fantasy", "Red Planet", {.35f, .2f, .15f}, {.85f, .6f, .4f}, {1, .85f, .7f}, 30, 180).haze(.45f, {.85f, .6f, .45f}).clouds(0, .2f, {.9f, .7f, .55f}).amb(.45f, {.7f, .5f, .4f}).sunSize(.7f),
            SkyB("Fantasy", "Twin Moons", {.05f, .02f, .15f}, {.35f, .15f, .4f}, {.7f, .6f, .9f}, 20, 60).haze(.2f, {.35f, .15f, .4f}).amb(.3f, {.35f, .25f, .5f}).stars(.9f).moon(30, 280, .6f, 4).sunSize(4),
            SkyB("Studio", "Atmosphere Off", {.35f, .35f, .38f}, {.35f, .35f, .38f}, {1, 1, 1}, 50, 140).haze(0, white).clouds(0, 0, white).amb(.5f, {.8f, .8f, .8f}).mode(3).hideSun(),
            SkyB("Studio", "Soft Sky", {.35f, .55f, .85f}, {.8f, .85f, .95f}, {1, .97f, .92f}, 45, 135).haze(.15f, {.8f, .85f, .95f}).clouds(.1f, 0, white).amb(.6f, {.65f, .7f, .85f}).mode(0),
            SkyB("Studio", "Darker Sky", {.08f, .22f, .6f}, {.55f, .65f, .85f}, {1, .97f, .9f}, 45, 135).haze(.1f, {.6f, .7f, .85f}).clouds(.2f, 0, white).amb(.45f, {.5f, .6f, .8f}).mode(1),
        };
        std::vector<Sky> out;
        for (auto& x : b) out.push_back(x.s);
        return out;
    }();
    return p;
}

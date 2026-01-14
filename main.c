#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

#define WINDOW_W 1200
#define WINDOW_H 675

#define MAX_CLOUDS    120
#define MAX_PARTICLES 700
#define MAX_CREATURES 38
#define MAX_NEBULAE   12
#define NUM_STARS     600
#define NUM_DEBRIS    300
#define NUM_PLANETS   6

#define SHIP_ROT_SPEED    0.10f
#define SHIP_THRUST       0.12f
#define HARVEST_RANGE     65.0f
#define CREATURE_DANGER_DIST 140.0f
#define FUEL_CONSUMPTION  0.085f
#define OVERHEAT_MAX      300.0f

// Overheat tuning
#define HEAT_GAIN_PER_THRUST      1.1f
#define HEAT_DECAY_NORMAL         0.7f
#define HEAT_DECAY_CRITICAL       0.35f
#define OVERHEAT_WARNING_THRESHOLD  0.80f
#define OVERHEAT_CRITICAL_THRESHOLD 1.00f
#define OVERHEAT_DAMAGE_PER_SEC     1.6f
#define OVERHEAT_THRUST_PENALTY     0.30f
#define OVERHEAT_DRAG_MULTIPLIER    0.94f

#define TRACTOR_RANGE     220.0f
#define COMBO_BOOST_THRESHOLD 8
#define COMBO_BOOST_DURATION 600

#define CLOUDS_PER_WAVE_BASE 45
#define WAVE_CREATURE_BONUS 4

typedef struct {
    float x, y, vx, vy, angle;
    float fuel, heat;
    int score, combo;
    int lives;
    bool tractor_active;
    float tractor_charge;
    bool combo_boost_active;
    int combo_boost_timer;
    float overheat_damage_accumulator;
} Ship;

typedef struct {
    float x, y, vx, vy;
    float size, density, phase;
    float pull_strength;
    int value;
    int active;
    Uint32 color;
} GasCloud;

typedef struct {
    float x, y, vx, vy, angle, wiggle;
    float size, hunt_phase, patrol_phase;
    float target_x, target_y;
    int type;
    int active;
    Uint32 color;
} NebulaCreature;

typedef struct {
    float x, y;
    float radius;
    float density, swirl, pulse;
    Uint32 color;
    bool active;
} Nebula;

typedef struct {
    float x, y, vx, vy;
    float life;
    Uint32 color;
    int active;
} Particle;

typedef struct { float base_x, base_y; int brightness, phase, size; } Star;
typedef struct { float base_x, base_y; float vx; int size; } Debris;
typedef struct { float base_x, base_y; float radius; Uint32 color; float spin; } Planet;
typedef struct {
    float base_x, base_y;
    float radius;
    float pulse_phase;
} Sun;

Ship ship;
GasCloud clouds[MAX_CLOUDS];
NebulaCreature creatures[MAX_CREATURES];
Nebula nebulas[MAX_NEBULAE];
Particle particles[MAX_PARTICLES];
Star stars[NUM_STARS];
Debris debris[NUM_DEBRIS];
Planet planets[NUM_PLANETS];
Sun sun;

int cloud_cnt = 0;
int creature_cnt = 0;
int particle_cnt = 0;
int nebula_cnt = 0;
int frame = 0;
float scrollX = 0.0f;
float danger_level = 0.0f;
int combo_timer = 0;

int wave = 1;
int clouds_collected_this_wave = 0;
int clouds_needed_for_next_wave = CLOUDS_PER_WAVE_BASE;
int wave_flash_timer = 0;
int current_wave_display_timer = 0;

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;

void wrap(float* x, float* y) {
    *x = fmodf(*x + WINDOW_W * 10, WINDOW_W);
    *y = fmodf(*y + WINDOW_H * 10, WINDOW_H);
}

float distance(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    if (fabsf(dx) > WINDOW_W / 2) dx -= (dx > 0 ? WINDOW_W : -WINDOW_W);
    if (fabsf(dy) > WINDOW_H / 2) dy -= (dy > 0 ? WINDOW_H : -WINDOW_H);
    return hypotf(dx, dy);
}

void spawn_particle(float x, float y, float vx, float vy, Uint32 color, float life) {
    if (particle_cnt >= MAX_PARTICLES) return;
    particles[particle_cnt++] = (Particle){x, y, vx, vy, life, color, 1};
}

void harvest_effect(float x, float y, int intensity) {
    for (int i = 0; i < 30 + intensity * 15; i++) {
        float ang = (float)i / (30 + intensity * 15) * 2 * M_PI;
        float speed = 3.5f + (rand() % 90) / 30.0f;
        Uint32 c = 0xAAEEFFAA | ((rand() % 120 + 135) << 24);
        spawn_particle(x, y, cosf(ang) * speed, sinf(ang) * speed * 0.7f, c, 60 + rand() % 50);
    }
}

void tractor_beam_effect(float x1, float y1, float x2, float y2) {
    for (int i = 0; i < 18; i++) {
        float t = (float)i / 18.0f + (rand() % 20)/1000.0f;
        float px = x1 + (x2 - x1) * t;
        float py = y1 + (y2 - y1) * t;
        float jitter_x = (rand() % 40 - 20) * 0.15f;
        float jitter_y = (rand() % 40 - 20) * 0.15f;
        Uint32 c = 0xCCEEFFFF | ((200 + (int)(sinf(frame * 0.5f + i) * 55)) << 24);
        spawn_particle(px + jitter_x, py + jitter_y, (rand() % 40 - 20) * 0.2f, (rand() % 40 - 20) * 0.2f, c, 40 + rand() % 20);
    }
}

void danger_trail(float x, float y) {
    for (int i = 0; i < 8; i++) {
        float ang = (rand() % 360) * M_PI / 180.0f;
        float speed = 5.0f + (rand() % 50) / 10.0f;
        Uint32 c = 0xFF4444FF | ((rand() % 100 + 140) << 24);
        spawn_particle(x, y, cosf(ang) * speed, sinf(ang) * speed, c, 30 + rand() % 25);
    }
}

void critical_overheat_effect() {
    float rear = ship.angle + M_PI;
    for (int i = 0; i < 8; i++) {
        float ang = rear + (rand() % 100 - 50) * 0.018f;
        float spd = 3.5f + (rand() % 60)/10.0f;
        Uint32 c = 0xAA444444 | ((90 + rand() % 80) << 24);
        spawn_particle(ship.x + cosf(rear)*20, ship.y + sinf(rear)*20,
                       cosf(ang)*spd + ship.vx*0.3f, sinf(ang)*spd + ship.vy*0.3f,
                       c, 60 + rand() % 50);
    }
    if (frame % 4 == 0) {
        for (int i = 0; i < 5; i++) {
            float ang = rand() * 2 * M_PI / RAND_MAX;
            float spd = 4.5f + (rand() % 60)/10.0f;
            Uint32 c = 0xFFFF8800 | ((180 + rand() % 75) << 24);
            spawn_particle(ship.x, ship.y,
                           cosf(ang)*spd, sinf(ang)*spd,
                           c, 30 + rand() % 25);
        }
    }
}

void thrust_flame() {
    float rear = ship.angle + M_PI;
    float px = ship.x + cosf(rear) * 22;
    float py = ship.y + sinf(rear) * 22;
    for (int i = 0; i < 14; i++) {
        float ang = rear + (rand() % 120 - 60) * 0.015f;
        float spd = 7.0f + (rand() % 70) / 10.0f;
        Uint32 c = (rand() % 3 == 0) ? 0xFFAA88FF : 0xEEFFCCFF;
        spawn_particle(px, py, cosf(ang) * spd + ship.vx * 0.25f, sinf(ang) * spd + ship.vy * 0.25f, c, 30 + rand() % 25);
    }
}

void trail_emit() {
    float speed = hypotf(ship.vx, ship.vy);
    if (speed < 3.5f || frame % 3 != 0) return;
    float rear = atan2f(ship.vy, ship.vx) + M_PI;
    float px = ship.x + cosf(rear) * 20;
    float py = ship.y + sinf(rear) * 20;
    for (int i = 0; i < 5; i++) {
        float ang = rear + (rand() % 100 - 50) * 0.012f;
        spawn_particle(px, py, cosf(ang) * (2.0f + speed * 0.3f), sinf(ang) * (2.0f + speed * 0.3f), 0x66DDFFFF, 40 + rand() % 35);
    }
}

void thick_line(int x1, int y1, int x2, int y2, int thickness) {
    if (thickness <= 1) {
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        return;
    }
    int dx = x2 - x1, dy = y2 - y1;
    float len = hypotf(dx, dy);
    if (len < 1.0f) return;
    float nx = -dy / len, ny = dx / len;
    for (int t = -thickness/2; t <= thickness/2; t++) {
        int ox = (int)(nx * t), oy = (int)(ny * t);
        SDL_RenderDrawLine(renderer, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
    }
}

const bool digit_segments[10][7] = {
    {1,1,1,0,1,1,1}, // 0
    {0,0,1,0,0,1,0}, // 1
    {1,0,1,1,1,0,1}, // 2
    {1,0,1,1,0,1,1}, // 3
    {0,1,1,1,0,1,0}, // 4
    {1,1,0,1,0,1,1}, // 5
    {1,1,0,1,1,1,1}, // 6
    {1,0,1,0,0,1,0}, // 7
    {1,1,1,1,1,1,1}, // 8
    {1,1,1,1,0,1,1}  // 9
};

void draw_7segment_digit(int bx, int by, int digit) {
    if (digit < 0 || digit > 9) return;
    
    int w = 20;
    int h = 32;
    int thick = 4;
    
    if (digit_segments[digit][0]) thick_line(bx, by, bx + w, by, thick);
    if (digit_segments[digit][1]) thick_line(bx, by, bx, by + h/2, thick);
    if (digit_segments[digit][2]) thick_line(bx + w, by, bx + w, by + h/2, thick);
    if (digit_segments[digit][3]) thick_line(bx, by + h/2, bx + w, by + h/2, thick);
    if (digit_segments[digit][4]) thick_line(bx, by + h/2, bx, by + h, thick);
    if (digit_segments[digit][5]) thick_line(bx + w, by + h/2, bx + w, by + h, thick);
    if (digit_segments[digit][6]) thick_line(bx, by + h, bx + w, by + h, thick);
}

bool is_critical_overheat() {
    return ship.heat >= OVERHEAT_MAX * OVERHEAT_CRITICAL_THRESHOLD;
}

bool is_overheat_warning() {
    return ship.heat >= OVERHEAT_MAX * OVERHEAT_WARNING_THRESHOLD;
}

void spawn_cloud() {
    if (cloud_cnt >= MAX_CLOUDS) return;
    GasCloud* c = &clouds[cloud_cnt++];
    c->active = 1;
    c->size = 18 + (rand() % 32);
    c->density = 0.65f + (rand() % 35) / 100.0f;
    c->phase = rand() * 2 * M_PI / RAND_MAX;
    c->pull_strength = 0.14f + (rand() % 70) / 1000.0f;
    c->value = 6 + (rand() % 10);
    
    int tries = 0;
    do {
        c->x = rand() % WINDOW_W;
        c->y = rand() % WINDOW_H;
    } while (distance(c->x, c->y, ship.x, ship.y) < 180 && ++tries < 50);
    
    float dir = rand() * 2 * M_PI / RAND_MAX;
    float speed = 0.4f + (rand() % 50) / 100.0f;
    c->vx = cosf(dir) * speed;
    c->vy = sinf(dir) * speed;
    
    int hue = 140 + rand() % 100;
    float sat = 0.9f + (rand() % 10)/100.0f;
    float val = 1.0f;
    float cmax = val * sat;
    float hp = hue / 60.0f;
    float x = cmax * (1 - fabsf(fmodf(hp, 2) - 1));
    Uint8 r,g,b;
    if (hp < 1) { r = cmax*255; g = x*255; b = 0; }
    else if (hp < 2) { r = x*255; g = cmax*255; b = 0; }
    else if (hp < 3) { r = 0; g = cmax*255; b = x*255; }
    else if (hp < 4) { r = 0; g = x*255; b = cmax*255; }
    else if (hp < 5) { r = x*255; g = 0; b = cmax*255; }
    else { r = cmax*255; g = 0; b = x*255; }
    c->color = (r << 16) | (g << 8) | b | 0xFF;
}

void spawn_creature() {
    if (creature_cnt >= MAX_CREATURES) return;
    NebulaCreature* n = &creatures[creature_cnt++];
    n->active = 1;
    n->size = 16 + rand() % 26;
    n->hunt_phase = 0;
    n->wiggle = rand() * 2 * M_PI / RAND_MAX;
    n->patrol_phase = rand() * 2 * M_PI / RAND_MAX;
    
    n->type = rand() % 3;
    
    int tries = 0;
    do {
        float side = rand() % 4;
        if (side == 0) { n->x = -100; n->y = rand() % WINDOW_H; }
        else if (side == 1) { n->x = WINDOW_W + 100; n->y = rand() % WINDOW_H; }
        else if (side == 2) { n->y = -100; n->x = rand() % WINDOW_W; }
        else { n->y = WINDOW_H + 100; n->x = rand() % WINDOW_W; }
    } while (tries++ < 80 && distance(n->x, n->y, ship.x, ship.y) < 300);
    
    float dir_to_ship = atan2f(ship.y - n->y, ship.x - n->x);
    float offset = (rand() % 100 - 50) / 100.0f * M_PI / 2;
    float target_dir = dir_to_ship + offset;
    float target_dist = 300 + rand() % 400;
    n->target_x = n->x + cosf(target_dir) * target_dist;
    n->target_y = n->y + sinf(target_dir) * target_dist;
    
    float dir = rand() * 2 * M_PI / RAND_MAX;
    float base_speed = (n->type == 0) ? 0.8f : (n->type == 1) ? 1.4f : 1.0f;
    n->vx = cosf(dir) * base_speed;
    n->vy = sinf(dir) * base_speed;
    n->angle = dir;
    
    if (n->type == 0) n->color = 0x88BBFFFF | ((170 + rand() % 50) << 24);
    else if (n->type == 1) n->color = 0xFF8888FF | ((140 + rand() % 60) << 24);
    else n->color = 0xCC88FFFF | ((130 + rand() % 70) << 24);
}

void spawn_nebula(int idx) {
    Nebula* n = &nebulas[idx];
    n->active = true;
    n->radius = 180 + rand() % 120;
    n->density = 0.4f + (rand() % 40)/100.0f;
    n->swirl = rand() * 2 * M_PI / RAND_MAX;
    n->pulse = 0.0f;
    n->x = WINDOW_W * (0.2f + (rand() % 1000)/10000.0f * 0.6f) - scrollX * 0.07f;
    n->y = 100 + rand() % 400;
    
    // Dark, starry blue/indigo — subtle, atmospheric, non-distracting
    int hue = 220 + rand() % 40;              // Deep blue to indigo
    float sat = 0.35f + (rand() % 15)/100.0f; // Low saturation
    float val = 0.45f + (rand() % 15)/100.0f; // Dark value
    float cmax = val * sat;
    float hp = hue / 60.0f;
    float x = cmax * (1 - fabsf(fmodf(hp, 2) - 1));
    Uint8 r,g,b;
    if (hp < 1) { r = cmax*255; g = x*255; b = 0; }
    else if (hp < 2) { r = x*255; g = cmax*255; b = 0; }
    else if (hp < 3) { r = 0; g = cmax*255; b = x*255; }
    else if (hp < 4) { r = 0; g = x*255; b = cmax*255; }
    else if (hp < 5) { r = x*255; g = 0; b = cmax*255; }
    else { r = cmax*255; g = 0; b = x*255; }
    n->color = (r << 16) | (g << 8) | b | 0x88; // Subtle transparency
}

void init_game() {
    srand(time(NULL));
    ship = (Ship){
        WINDOW_W / 2.0f, WINDOW_H / 2.0f, 0, 0, -M_PI / 2,
        1000.0f, 0, 0, 0, 3, false, 0, false, 0,
        0.0f
    };
    cloud_cnt = creature_cnt = particle_cnt = nebula_cnt = 0;
    frame = 0;
    scrollX = 0.0f;
    danger_level = 0.0f;
    wave = 1;
    clouds_collected_this_wave = 0;
    clouds_needed_for_next_wave = CLOUDS_PER_WAVE_BASE;
    wave_flash_timer = 0;
    current_wave_display_timer = 0;
    
    for (int i = 0; i < 35; i++) spawn_cloud();
    for (int i = 0; i < MAX_NEBULAE; i++) spawn_nebula(i);
    
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].base_x = (rand() % 90000) - 45000;
        stars[i].base_y = rand() % WINDOW_H;
        stars[i].brightness = 110 + rand() % 145;
        stars[i].phase = rand() % 256;
        stars[i].size = 1 + (rand() % 3);
    }
    for (int i = 0; i < NUM_DEBRIS; i++) {
        debris[i].base_x = (rand() % 120000) - 60000;
        debris[i].base_y = rand() % WINDOW_H;
        debris[i].vx = 0.25f + (rand() % 80)/100.0f;
        debris[i].size = 1 + rand() % 3;
    }
    
    for (int i = 0; i < NUM_PLANETS; i++) {
        planets[i].base_x = 800 + (rand() % 1200);
        planets[i].base_y = 100 + rand() % 400;
        planets[i].radius = 28 + rand() % 28;
        planets[i].color = (rand() % 128 + 64) << 16 | (rand() % 128 + 64) << 8 | (rand() % 255);
        planets[i].spin = 0;
    }
    
    sun.base_x = WINDOW_W * 0.7f;
    sun.base_y = WINDOW_H * 0.25f;
    sun.radius = 110;
    sun.pulse_phase = 0;
    
    for (int i = 0; i < 8; i++) spawn_creature();
}

void update() {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    int left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    int right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    int thrust = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    static bool prev_tractor = false;
    bool tractor = keys[SDL_SCANCODE_SPACE];
    
    frame++;
    scrollX += 0.9f + danger_level * 0.12f;
    sun.pulse_phase += 0.018f;
    danger_level = fminf(1.3f, danger_level + 0.00008f * cloud_cnt);
    
    if (tractor && !prev_tractor) ship.tractor_active = true;
    if (tractor) {
        ship.tractor_charge += 0.25f;
        if (!ship.combo_boost_active) ship.fuel -= 0.12f;
    } else {
        ship.tractor_active = false;
        ship.tractor_charge = fmaxf(0, ship.tractor_charge - 0.4f);
    }
    prev_tractor = tractor;
    
    if (ship.combo >= COMBO_BOOST_THRESHOLD && !ship.combo_boost_active) {
        ship.combo_boost_active = true;
        ship.combo_boost_timer = COMBO_BOOST_DURATION;
    }
    if (ship.combo_boost_active) {
        ship.combo_boost_timer--;
        if (ship.combo_boost_timer <= 0) {
            ship.combo_boost_active = false;
        }
    }
    
    for (int i = 0; i < NUM_PLANETS; i++) {
        planets[i].base_x -= 0.11f + danger_level * 0.007f;
        if (planets[i].base_x < -400) {
            planets[i].base_x = WINDOW_W + 600 + rand() % 400;
            planets[i].base_y = 120 + rand() % 350;
        }
    }
    
    if (left) ship.angle -= SHIP_ROT_SPEED;
    if (right) ship.angle += SHIP_ROT_SPEED;
    
    float effective_thrust = SHIP_THRUST;
    if (is_critical_overheat()) effective_thrust *= OVERHEAT_THRUST_PENALTY;
    
    if (thrust && ship.fuel > 5.0f) {
        ship.vx += cosf(ship.angle) * effective_thrust;
        ship.vy += sinf(ship.angle) * effective_thrust;
        ship.fuel -= FUEL_CONSUMPTION;
        ship.heat += HEAT_GAIN_PER_THRUST;
        thrust_flame();
    }
    
    float decay = is_critical_overheat() ? HEAT_DECAY_CRITICAL : HEAT_DECAY_NORMAL;
    ship.heat = fmaxf(0, ship.heat - decay);
    
    ship.x += ship.vx;
    ship.y += ship.vy;
    
    if (is_critical_overheat()) {
        ship.vx *= OVERHEAT_DRAG_MULTIPLIER;
        ship.vy *= OVERHEAT_DRAG_MULTIPLIER;
        critical_overheat_effect();
        
        ship.overheat_damage_accumulator += OVERHEAT_DAMAGE_PER_SEC / 60.0f;
        if (ship.overheat_damage_accumulator >= 1.0f) {
            int damage = (int)ship.overheat_damage_accumulator;
            ship.lives -= damage;
            ship.overheat_damage_accumulator -= damage;
            danger_trail(ship.x, ship.y);
            if (ship.lives <= 0) {
                printf("Game Over! (Overheated to death) Final Score: %d\n", ship.score);
                init_game();
            }
        }
    } else {
        ship.vx *= 0.985f;
        ship.vy *= 0.985f;
        ship.overheat_damage_accumulator = fmaxf(0, ship.overheat_damage_accumulator - 0.4f);
    }
    
    wrap(&ship.x, &ship.y);
    trail_emit();
    
    ship.fuel = fminf(1000.0f, ship.fuel + 0.35f);
    
    for (int i = 0; i < cloud_cnt; i++) {
        if (!clouds[i].active) continue;
        GasCloud* c = &clouds[i];
        
        float current_range = ship.combo_boost_active ? TRACTOR_RANGE * 1.6f : TRACTOR_RANGE;
        float current_pull = ship.combo_boost_active ? 1.5f : 1.0f;
        
        if (ship.tractor_active && distance(c->x, c->y, ship.x, ship.y) < current_range) {
            float dx = ship.x - c->x;
            float dy = ship.y - c->y;
            float dist = hypotf(dx, dy);
            if (dist > 0) {
                float pull = c->pull_strength * fminf(ship.tractor_charge * 0.02f, current_pull);
                c->vx += (dx / dist) * pull;
                c->vy += (dy / dist) * pull;
                tractor_beam_effect(ship.x, ship.y, c->x, c->y);
            }
        }
        
        c->x += c->vx;
        c->y += c->vy;
        c->phase += 0.08f;
        c->vx *= 0.97f;
        c->vy *= 0.97f;
        wrap(&c->x, &c->y);
        
        if (distance(c->x, c->y, ship.x, ship.y) < HARVEST_RANGE) {
            int points = c->value * (1 + ship.combo * 0.2f);
            ship.score += points;
            harvest_effect(c->x, c->y, c->value);
            c->active = 0;
            clouds[i] = clouds[--cloud_cnt];
            i--;
            ship.combo++;
            combo_timer = 300;
            clouds_collected_this_wave++;
            
            if (clouds_collected_this_wave >= clouds_needed_for_next_wave) {
                wave++;
                clouds_collected_this_wave = 0;
                clouds_needed_for_next_wave = CLOUDS_PER_WAVE_BASE + wave * 18;
                wave_flash_timer = 180;
                current_wave_display_timer = 180;
                
                for (int j = 0; j < WAVE_CREATURE_BONUS + wave / 2; j++) {
                    spawn_creature();
                }
                
                danger_level += 0.2f;
            }
            continue;
        }
    }
    
    while (cloud_cnt < 40 + (int)(danger_level * 35)) spawn_cloud();
    
    for (int i = 0; i < creature_cnt; i++) {
        if (!creatures[i].active) continue;
        NebulaCreature* n = &creatures[i];
        
        n->hunt_phase += 0.04f;
        n->wiggle += 0.09f;
        n->patrol_phase += 0.025f;
        
        float dist_to_ship = distance(n->x, n->y, ship.x, ship.y);
        
        if (frame % 200 == i % 200) {
            if (dist_to_ship > 600.0f) {
                float dir_to_ship = atan2f(ship.y - n->y, ship.x - n->x);
                float offset = (rand() % 100 - 50) / 100.0f * M_PI / 2;
                float target_dir = dir_to_ship + offset;
                float target_dist = 300 + rand() % 400;
                n->target_x = n->x + cosf(target_dir) * target_dist;
                n->target_y = n->y + sinf(target_dir) * target_dist;
            } else {
                float random_dir = rand() * 2 * M_PI / RAND_MAX;
                float target_dist = 200 + rand() % 300;
                n->target_x = n->x + cosf(random_dir) * target_dist;
                n->target_y = n->y + sinf(random_dir) * target_dist;
            }
        }
        
        float dir;
        float dx = ship.x - n->x;
        float dy = ship.y - n->y;
        if (fabsf(dx) > WINDOW_W / 2) dx -= (dx > 0 ? WINDOW_W : -WINDOW_W);
        if (fabsf(dy) > WINDOW_H / 2) dy -= (dy > 0 ? WINDOW_H : -WINDOW_H);
        dir = atan2f(dy, dx);
        
        if (n->type == 0) {
            if (dist_to_ship < 420) {
                n->vx += cosf(dir) * 0.028f;
                n->vy += sinf(dir) * 0.028f;
            } else {
                n->vx += cosf(dir) * 0.03f;
                n->vy += sinf(dir) * 0.03f;
            }
        } else if (n->type == 1) {
            if (dist_to_ship < 500) {
                n->vx += cosf(dir) * 0.045f + sinf(n->wiggle) * 0.06f;
                n->vy += sinf(dir) * 0.045f + cosf(n->wiggle) * 0.06f;
            } else {
                n->vx += cosf(dir) * 0.035f + sinf(n->wiggle) * 0.03f;
                n->vy += sinf(dir) * 0.035f + cosf(n->wiggle) * 0.03f;
            }
        } else {
            if (dist_to_ship < 380) {
                float offset = (dist_to_ship < 180) ? -M_PI/2 : M_PI/2;
                dir += offset + sinf(n->wiggle)*0.3f;
                n->vx += cosf(dir) * 0.036f;
                n->vy += sinf(dir) * 0.036f;
            } else {
                n->vx += cosf(dir) * 0.03f;
                n->vy += sinf(dir) * 0.03f;
            }
        }
        
        n->angle = atan2f(n->vy, n->vx);
        n->x += n->vx;
        n->y += n->vy;
        n->vx *= 0.975f;
        n->vy *= 0.975f;
        wrap(&n->x, &n->y);
        
        if (dist_to_ship < n->size + 28) {
            ship.lives--;
            ship.fuel *= 0.4f;
            ship.heat = OVERHEAT_MAX * 0.92f;
            danger_trail(ship.x, ship.y);
            n->active = 0;
            creatures[i] = creatures[--creature_cnt];
            ship.combo = 0;
            if (ship.lives <= 0) {
                printf("Game Over! Final Score: %d\n", ship.score);
                init_game();
            }
        }
    }
    
    if (frame % 520 == 0 && creature_cnt < 14 + (int)(danger_level * 12)) {
        spawn_creature();
    }
    
    for (int i = particle_cnt - 1; i >= 0; i--) {
        Particle* p = &particles[i];
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 0.06f * ((p->color >> 24) / 255.0f);
        p->vx *= 0.98f;
        p->life -= 1.2f;
        if (p->life <= 0) {
            particles[i] = particles[--particle_cnt];
        } else {
            wrap(&p->x, &p->y);
        }
    }
    
    if (combo_timer > 0) combo_timer--;
    else ship.combo = 0;
    
    if (wave_flash_timer > 0) wave_flash_timer--;
    if (current_wave_display_timer > 0) current_wave_display_timer--;
}

void draw_ship() {
    float heat_ratio = ship.heat / (float)OVERHEAT_MAX;
    float heat_glow = fminf(heat_ratio, 1.3f);

    Uint8 r = 255;
    Uint8 g = (Uint8)(255 - heat_glow * 160);
    Uint8 b = (Uint8)(120 + heat_glow * 40);
    Uint8 alpha = 220 + (Uint8)(35 * sinf(frame * 0.25f));

    if (is_critical_overheat()) {
        if ((frame / 5) % 2 == 0) {
            r = 255; g = 50; b = 30;
            alpha = 255;
        } else {
            r = 230; g = 90; b = 50;
            alpha = 200;
        }
    } else if (is_overheat_warning()) {
        g = (Uint8)(g * 0.5f + 100);
        b = (Uint8)(b * 0.3f + 30);
    }

    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
    
    float nx = ship.x + cosf(ship.angle) * 30;
    float ny = ship.y + sinf(ship.angle) * 30;
    float lx = ship.x + cosf(ship.angle + 2.4f) * 24;
    float ly = ship.y + sinf(ship.angle + 2.4f) * 24;
    float rx = ship.x + cosf(ship.angle - 2.4f) * 24;
    float ry = ship.y + sinf(ship.angle - 2.4f) * 24;
    float corex = ship.x + cosf(ship.angle) * 14;
    float corey = ship.y + sinf(ship.angle) * 14;
    
    thick_line((int)nx, (int)ny, (int)lx, (int)ly, 7);
    thick_line((int)lx, (int)ly, (int)corex, (int)corey, 7);
    thick_line((int)corex, (int)corey, (int)rx, (int)ry, 7);
    thick_line((int)rx, (int)ry, (int)nx, (int)ny, 7);
    
    SDL_SetRenderDrawColor(renderer, 255, 240 - (int)(heat_glow * 120), 180, 200);
    for (int r = 0; r < 12; r++) {
        SDL_RenderDrawLine(renderer, (int)(ship.x - r), (int)ship.y, (int)(ship.x + r), (int)ship.y);
        SDL_RenderDrawLine(renderer, (int)ship.x, (int)(ship.y - r), (int)ship.x, (int)(ship.y + r));
    }
    
    if (ship.tractor_active) {
        float pulse = sinf(frame * 0.3f) * 0.4f + 0.6f;
        Uint8 beam_a = (Uint8)(180 + 75 * pulse);
        SDL_SetRenderDrawColor(renderer, 120, 240, 255, beam_a);
        for (int r = 0; r < 16; r += 3) {
            SDL_RenderDrawLine(renderer, (int)(ship.x - r*1.4f), (int)ship.y, 
                                     (int)(ship.x + r*1.4f), (int)ship.y);
        }
    }
    
    if (ship.combo > 0) {
        float pulse = sinf(frame * 0.25f) * 0.5f + 0.5f;
        Uint8 aura_a = (Uint8)(140 + 115 * pulse);
        SDL_SetRenderDrawColor(renderer, 140, 255, 220, aura_a);
        for (int r = 0; r < 14; r += 2) {
            SDL_RenderDrawLine(renderer, (int)(ship.x - r), (int)(ship.y - r), 
                                     (int)(ship.x + r), (int)(ship.y - r));
            SDL_RenderDrawLine(renderer, (int)(ship.x - r), (int)(ship.y + r), 
                                     (int)(ship.x + r), (int)(ship.y + r));
        }
    }

    if (is_overheat_warning()) {
        Uint8 glow_a = (Uint8)(80 + 120 * sinf(frame * 0.45f));
        SDL_SetRenderDrawColor(renderer, 255, 140, 40, glow_a);
        for (int r = 0; r < 22; r += 4) {
            SDL_RenderDrawLine(renderer, (int)(ship.x - r*1.6f), (int)ship.y,
                                     (int)(ship.x + r*1.6f), (int)ship.y);
        }
    }
}

void draw_gas_cloud(GasCloud* c) {
    float pulse = 0.8f + 0.2f * sinf(c->phase + frame * 0.14f);
    int rad = (int)(c->size * pulse * c->density);
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 50);
    for (int dy = -rad*1.3f; dy <= rad*1.3f; dy += 7) {
        int w = (int)(sqrtf(rad*rad*1.7f - dy*dy) * 0.35f);
        if (w > 0)
            SDL_RenderDrawLine(renderer, (int)(c->x - w), (int)(c->y + dy), (int)(c->x + w), (int)(c->y + dy));
    }
    
    SDL_SetRenderDrawColor(renderer, (c->color>>16)&255, (c->color>>8)&255, c->color&255, 255);
    for (int dy = -rad; dy <= rad; dy += 3) {
        int w = (int)(sqrtf(rad*rad - dy*dy) * c->density * 0.9f);
        SDL_RenderDrawLine(renderer, (int)(c->x - w), (int)(c->y + dy), (int)(c->x + w), (int)(c->y + dy));
    }
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
    for (int r = 0; r < 8; r++) {
        SDL_RenderDrawLine(renderer, (int)(c->x - r), (int)c->y, (int)(c->x + r), (int)c->y);
        SDL_RenderDrawLine(renderer, (int)c->x, (int)(c->y - r), (int)c->x, (int)(c->y + r));
    }
}

void draw_nebula_creature(NebulaCreature* n) {
    float pulse = 0.85f + 0.15f * sinf(frame * 0.18f + n->hunt_phase);
    int size = (int)(n->size * pulse);
    
    SDL_SetRenderDrawColor(renderer, (n->color>>16)&255, (n->color>>8)&255, n->color&255, (n->color>>24)&255);
    for (int dy = -size; dy <= size; dy += 3) {
        int w = (int)sqrtf(size*size - dy*dy);
        SDL_RenderDrawLine(renderer, (int)(n->x - w), (int)(n->y + dy), (int)(n->x + w), (int)(n->y + dy));
    }
    
    if (n->type == 0) {
        SDL_SetRenderDrawColor(renderer, 200, 220, 255, 180);
        for (int i = 0; i < 5; i++) {
            float ang = n->angle + i * M_PI / 2.5f + sinf(n->wiggle + i) * 0.3f;
            int ex = (int)(n->x + cosf(ang) * (size + 10));
            int ey = (int)(n->y + sinf(ang) * (size + 10));
            thick_line((int)n->x, (int)n->y, ex, ey, 2);
        }
    } else if (n->type == 1) {
        SDL_SetRenderDrawColor(renderer, 255, 120, 120, 220);
        for (int i = 0; i < 6; i++) {
            float ang = n->angle + i * M_PI / 3 + sinf(n->wiggle + i) * 0.4f;
            int ex = (int)(n->x + cosf(ang) * (size + 16));
            int ey = (int)(n->y + sinf(ang) * (size + 16));
            thick_line((int)n->x, (int)n->y, ex, ey, 4);
        }
    } else {
        SDL_SetRenderDrawColor(renderer, 180, 100, 220, 200);
        for (int i = 0; i < 8; i++) {
            float ang = n->angle + i * M_PI / 4 + sinf(n->wiggle * 0.8f + i) * 0.6f;
            int ex = (int)(n->x + cosf(ang) * (size + 18));
            int ey = (int)(n->y + sinf(ang) * (size + 18));
            thick_line((int)n->x, (int)n->y, ex, ey, 3);
        }
    }
    
    float dist_to_ship = distance(n->x, n->y, ship.x, ship.y);
    if (dist_to_ship < CREATURE_DANGER_DIST) {
        Uint8 glow = (Uint8)(255 * (1.0f - dist_to_ship / CREATURE_DANGER_DIST));
        SDL_SetRenderDrawColor(renderer, 255, 80, 80, glow);
        for (int r = 0; r < 20; r += 4) {
            SDL_RenderDrawLine(renderer, (int)(n->x - r), (int)n->y, (int)(n->x + r), (int)n->y);
        }
    }
}

void draw_nebula(Nebula* n) {
    float nx = n->x - scrollX * 0.08f;
    if (nx < -400 || nx > WINDOW_W + 400) return;
    
    n->swirl += 0.003f;
    n->pulse += 0.012f;
    
    float brightness_pulse = 0.9f + 0.1f * sinf(n->pulse);
    Uint8 alpha_base = (Uint8)(0x88 * brightness_pulse);
    
    int r = (int)n->radius;
    SDL_SetRenderDrawColor(renderer, (n->color>>16)&255, (n->color>>8)&255, n->color&255, alpha_base);
    for (int dy = -r; dy <= r; dy += 5) {
        float swirl_off = sinf((dy * 0.025f + n->swirl * 3) * 1.7f) * n->density * 35;
        int hw = (int)(sqrtf(r*r - dy*dy) + swirl_off);
        SDL_RenderDrawLine(renderer, (int)(nx - hw), (int)(n->y + dy), (int)(nx + hw), (int)(n->y + dy));
    }
    
    // Very subtle core
    SDL_SetRenderDrawColor(renderer, 180, 190, 255, (Uint8)(70 * brightness_pulse));
    for (int dy = -r/4; dy <= r/4; dy += 10) {
        int hw = (int)(sqrtf((r/4)*(r/4) - dy*dy) * 1.2f);
        SDL_RenderDrawLine(renderer, (int)(nx - hw), (int)(n->y + dy), (int)(nx + hw), (int)(n->y + dy));
    }
}

void render() {
    SDL_SetRenderDrawColor(renderer, 3, 3, 12, 255);
    SDL_RenderClear(renderer);
    
    for (int i = 0; i < NUM_STARS; i++) {
        float px = stars[i].base_x - scrollX * 0.18f;
        px = fmodf(px + 120000, 240000) - 120000;
        if (px < -60 || px > WINDOW_W + 60) continue;
        float twinkle = 0.65f + 0.35f * sinf(frame * 0.09f + stars[i].phase);
        int br = (int)(stars[i].brightness * twinkle);
        SDL_SetRenderDrawColor(renderer, br, br, br + 40, 255);
        int sx = (int)px, sy = (int)stars[i].base_y;
        for (int s = -stars[i].size; s <= stars[i].size; s++) {
            SDL_RenderDrawPoint(renderer, sx + s, sy);
            SDL_RenderDrawPoint(renderer, sx, sy + s);
        }
    }
    
    for (int i = 0; i < NUM_DEBRIS; i++) {
        float px = debris[i].base_x - scrollX * 0.45f;
        px = fmodf(px + 180000, 360000) - 180000;
        if (px < -40 || px > WINDOW_W + 40) continue;
        int g = 100 + (int)(debris[i].vx * 180 + sinf(frame * 0.06f + i * 0.1f) * 35);
        SDL_SetRenderDrawColor(renderer, g, g + 20, 180, 200);
        for(int s = 0; s < debris[i].size * 2 + 1; s++) {
            SDL_RenderDrawPoint(renderer, (int)px + s, (int)debris[i].base_y);
        }
    }
    
    float sun_pulse = 1.0f + 0.12f * sinf(sun.pulse_phase);
    float sun_r = sun.radius * sun_pulse;
    SDL_SetRenderDrawColor(renderer, 255, 255, 180, 90);
    for (int r = (int)sun_r + 55; r > (int)sun_r + 18; r -= 12) {
        for (int dy = -r; dy <= r; dy += 9) {
            int hw = (int)sqrtf(r*r - dy*dy);
            SDL_RenderDrawLine(renderer, (int)sun.base_x - hw, (int)(sun.base_y + dy),
                                     (int)sun.base_x + hw, (int)(sun.base_y + dy));
        }
    }
    SDL_SetRenderDrawColor(renderer, 255, 240, 140, 255);
    for (int dy = -sun_r; dy <= sun_r; dy += 5) {
        int hw = (int)sqrtf(sun_r*sun_r - dy*dy);
        SDL_RenderDrawLine(renderer, (int)sun.base_x - hw, (int)(sun.base_y + dy),
                                 (int)sun.base_x + hw, (int)(sun.base_y + dy));
    }
    
    // Draw nebulae
    for (int i = 0; i < MAX_NEBULAE; i++) {
        if (nebulas[i].active) {
            draw_nebula(&nebulas[i]);
        }
    }
    
    for (int i = 0; i < NUM_PLANETS; i++) {
        Planet* p = &planets[i];
        float px = p->base_x - scrollX * 0.12f;
        if (px < -350 || px > WINDOW_W + 350) continue;
        
        p->spin += 0.0018f;
        int r = (int)p->radius;
        for (int dy = -r; dy <= r; dy += 4) {
            int hw = (int)sqrtf(r*r - dy*dy);
            float swirl = sinf(dy * 0.035f + p->spin * 5);
            Uint8 alpha = 140 + (int)(95 * swirl);
            SDL_SetRenderDrawColor(renderer, (p->color>>16)&255, (p->color>>8)&255, p->color&255, alpha);
            SDL_RenderDrawLine(renderer, (int)(px - hw), (int)(p->base_y + dy), (int)(px + hw), (int)(p->base_y + dy));
        }
    }
    
    for (int i = 0; i < cloud_cnt; i++) if (clouds[i].active) draw_gas_cloud(&clouds[i]);
    for (int i = 0; i < creature_cnt; i++) if (creatures[i].active) draw_nebula_creature(&creatures[i]);
    
    for (int i = 0; i < particle_cnt; i++) {
        Particle* p = &particles[i];
        int alpha = (int)(255 * (p->life / 60.0f));
        if (alpha < 25) continue;
        SDL_SetRenderDrawColor(renderer, (p->color>>16)&255, (p->color>>8)&255, p->color&255, alpha);
        int px = (int)p->x, py = (int)p->y;
        SDL_RenderDrawPoint(renderer, px, py);
        if (alpha > 100) {
            SDL_RenderDrawPoint(renderer, px+1, py);
            SDL_RenderDrawPoint(renderer, px, py+1);
        }
    }
    
    draw_ship();
    
    // FIXED SCORE DISPLAY: digits grow from the RIGHT (least significant first)
    int display_score = ship.score % 1000000;
    char score_buf[7];
    sprintf(score_buf, "%06d", display_score);
    
    int digit_w = 32;
    int digit_spacing = digit_w + 10;
    int start_x = WINDOW_W - 60;                        // right edge anchor
    int start_y = 10;
    
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int i = 0; i < 6; i++) {
        int digit = score_buf[5 - i] - '0';             // read from right to left
        int bx = start_x - i * digit_spacing;           // place digits from right
        draw_7segment_digit(bx, start_y, digit);
    }
    
    // Lives (unchanged)
    for (int i = 0; i < ship.lives; i++) {
        int lx = 40 + i * 45;
        SDL_SetRenderDrawColor(renderer, 180, 255, 180, 255);
        thick_line(lx, 20, lx + 30, 20, 5);
        thick_line(lx + 8, 30, lx + 22, 30, 5);
    }
    
    // Fuel bar (unchanged)
    int fuel_fill = (int)((ship.fuel / 1000.0f) * 220);
    SDL_SetRenderDrawColor(renderer, 40, 60, 80, 220);
    SDL_RenderFillRect(renderer, &(SDL_Rect){30, 70, 240, 18});
    SDL_SetRenderDrawColor(renderer, 80, 200, 255, 255);
    SDL_RenderFillRect(renderer, &(SDL_Rect){33, 73, fuel_fill, 12});
    
    // Heat bar (unchanged)
    int heat_fill = (int)((ship.heat / OVERHEAT_MAX) * 220);
    SDL_SetRenderDrawColor(renderer, 100, 40, 40, 220);
    SDL_RenderFillRect(renderer, &(SDL_Rect){30, 95, 240, 14});
    if (is_critical_overheat()) {
        SDL_SetRenderDrawColor(renderer, 255, 60, 40, 255);
    } else if (is_overheat_warning()) {
        SDL_SetRenderDrawColor(renderer, 255, 140, 40, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 100, 80, 255);
    }
    SDL_RenderFillRect(renderer, &(SDL_Rect){33, 98, heat_fill, 8});
    
    // Combo meter (unchanged)
    if (ship.combo > 0) {
        int combo_w = ship.combo * 10;
        int max_w = 200;
        if (combo_w > max_w) combo_w = max_w;
        
        float pulse = sinf(frame * 0.35f) * 0.5f + 0.5f;
        Uint8 r = 255;
        Uint8 g = 220 + (Uint8)(35 * pulse);
        Uint8 b = 100 + (Uint8)(100 * pulse);
        Uint8 a = (Uint8)(200 + 55 * pulse);
        
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &(SDL_Rect){WINDOW_W/2 - combo_w/2, 20, combo_w, 24});
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(100 + 155 * pulse));
        SDL_RenderDrawRect(renderer, &(SDL_Rect){WINDOW_W/2 - combo_w/2 - 3, 17, combo_w + 6, 30});
        
        if (ship.combo_boost_active) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255);
            for (int off = 0; off < 8; off += 2) {
                SDL_RenderDrawRect(renderer, &(SDL_Rect){WINDOW_W/2 - combo_w/2 - 8 - off, 12 - off, combo_w + 16 + off*2, 40 + off*2});
            }
        }
    }
    
    // Wave indicator (numbers only, bottom-right)
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    int tx = WINDOW_W - 100;
    int ty = WINDOW_H - 50;
    int wave_digit_x = tx;
    draw_7segment_digit(wave_digit_x + 35, ty - 8, wave % 10);
    if (wave >= 10) draw_7segment_digit(wave_digit_x, ty - 8, (wave / 10) % 10);
    
    // Flash yellow when advancing
    if (current_wave_display_timer > 0) {
        current_wave_display_timer--;
        Uint8 flash_alpha = (Uint8)(180 + 75 * sinf(frame * 0.5f));
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, flash_alpha);
        draw_7segment_digit(wave_digit_x + 35, ty - 8, wave % 10);
        if (wave >= 10) draw_7segment_digit(wave_digit_x, ty - 8, (wave / 10) % 10);
    }
    
    SDL_RenderPresent(renderer);
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Nebula Harvester", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    init_game();
    
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
        }
        
        update();
        render();
        SDL_Delay(16);
    }
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
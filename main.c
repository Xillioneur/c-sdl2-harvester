#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

#define WINDOW_W 1200
#define WINDOW_H 675

#define MAX_CLOUDS 120
#define MAX_CREATURES 38

#define SHIP_ROT_SPEED 0.10f
#define SHIP_THRUST 0.12f
#define HARVEST_RANGE 65.0f
#define FUEL_CONSUMPTION 0.85f
#define TRACTOR_FUEL_COST  0.60f
#define OVERHEAT_MAX 300.0f

#define HEAT_GAIN_PER_THRUST 1.1f
#define HEAT_DECAY_NORMAL 0.7f
#define HEAT_DECAY_CRITICAL 0.35f
#define OVERHEAT_WARNING_THRESHOLD 0.80f
#define OVERHEAT_CRITICAL_THRESHOLD 1.00f
#define OVERHEAT_THRUST_PENALTY 0.30f
#define OVERHEAT_DRAG_MULTIPLIER 0.94f

#define TRACTOR_RANGE 220.0f
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
    float base_x, base_y;
    float radius;
    float pulse_phase;
} Sun;

Ship ship;
GasCloud clouds[MAX_CLOUDS];
NebulaCreature creatures[MAX_CREATURES];

int cloud_cnt = 0;
int creature_cnt = 0;
int frame = 0;
float danger_level = 0.0f;
int combo_timer;

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

void init_game() {
    srand(time(NULL));
    ship = (Ship) {
        WINDOW_W / 2.0f, WINDOW_H / 2.0f, 0, 0, -M_PI / 2,
        1000.0f, 0, 0, 0, 3, false, 0, false, 0,
        0.0f
    };
    cloud_cnt = creature_cnt = 0;
    frame = 0;
    danger_level = 0.0f;
    wave = 1;
    clouds_collected_this_wave = 0;
    clouds_needed_for_next_wave = CLOUDS_PER_WAVE_BASE;
    wave_flash_timer = 0;

    for (int i = 0; i < 35; i++) spawn_cloud();

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

    if (tractor && !prev_tractor) ship.tractor_active = true;
    if (tractor) {
        ship.tractor_charge += 0.25f;
        if (!ship.combo_boost_active) ship.fuel -= TRACTOR_FUEL_COST;
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

    if (left) ship.angle -= SHIP_ROT_SPEED;
    if (right) ship.angle += SHIP_ROT_SPEED;

    float effective_thrust = SHIP_THRUST;
    if (is_critical_overheat()) effective_thrust *= OVERHEAT_THRUST_PENALTY;

    if (thrust && ship.fuel > 5.0f) {
        ship.vx += cosf(ship.angle) * SHIP_THRUST;
        ship.vy += sinf(ship.angle) * SHIP_THRUST;
        ship.fuel -= FUEL_CONSUMPTION;
        ship.heat += HEAT_GAIN_PER_THRUST;
    }

    float decay = is_critical_overheat() ? HEAT_DECAY_CRITICAL : HEAT_DECAY_NORMAL;
    ship.heat = fmaxf(0, ship.heat - decay);

    ship.x += ship.vx;
    ship.y += ship.vy;

    if (is_critical_overheat()) {
        ship.vx *= OVERHEAT_DRAG_MULTIPLIER;
        ship.vy *= OVERHEAT_DRAG_MULTIPLIER;
        // TODO: critical overheat effect
        // TODO: Damage over time
    } else {
        ship.vx *= 0.985f;
        ship.vy *= 0.985f;
        ship.overheat_damage_accumulator = fmaxf(0, ship.overheat_damage_accumulator - 0.4f);
    }

    wrap(&ship.x, &ship.y);

    if (!thrust) {
        ship.fuel = fminf(1000.0f, ship.fuel + 0.08f);
    }

    // Cloud harvesting and wave logic
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
                // TODO: Tractor beam effect
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
            // TODO: Harvset effect
            c->active = 0;
            clouds[i] = clouds[--cloud_cnt];
            i--;
            ship.combo++;
            ship.fuel = fminf(1000.0f, ship.fuel + 60.0f + c->value * 8.0f);
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
        dir = atan2(dy, dx);

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
        n->vx *= 0.975;
        n->vy *= 0.975;
        wrap(&n->x, &n->y);

        if (dist_to_ship < n->size + 28) {
            ship.lives--;
            ship.fuel *= 0.4f;
            ship.heat = OVERHEAT_MAX * 0.92f;
            // TODO: Add danger trail
            n->active = 0;
            creatures[i] = creatures[--creature_cnt];
            ship.combo = 0;
            if (ship.lives <= 0) {
                printf("Game Over! Final Score: %d\n", ship.score);
                init_game();
            }
        }
    }

    if (frame % 520 == 0 && creature_cnt < 14 + (int)(danger_level)) {
        spawn_creature();
    }

    if (combo_timer > 0) combo_timer--;
    else ship.combo = 0;

    if (wave_flash_timer > 0) wave_flash_timer--;
}

void draw_ship() {
    float heat_ratio = ship.heat / (float)OVERHEAT_MAX;
    float heat_glow = fminf(heat_ratio, 1.3f);

    Uint8 r = 255;
    Uint8 g = (Uint8)(255 - heat_glow * 160);
    Uint8 b = (Uint8)(120 + heat_glow * 40);
    Uint8 alpha = 220 + (Uint8)(35 * sinf(frame * 0.25f));

    if (is_critical_overheat()) {
        if ((frame / 5 ) % 2 == 0) {
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

void draw_nebula_creature(NebulaCreature *n) {
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
}

void render() {
    SDL_SetRenderDrawColor(renderer, 3, 3, 12, 255);
    SDL_RenderClear(renderer);

    for (int i = 0; i < cloud_cnt; i++) if (clouds[i].active) draw_gas_cloud(&clouds[i]);
    for (int i = 0; i < creature_cnt; i++) if (creatures[i].active) draw_nebula_creature(&creatures[i]);

    draw_ship();

    // Score
    int display_score = ship.score % 1000000;
    char score_buf[7];
    sprintf(score_buf, "%06d", display_score);

    int digit_w = 32;
    int start_x = WINDOW_W - 60;
    int start_y = 10;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int i = 0; i < 6; i++) {
        int digit = score_buf[5 - i] - '0';
        int bx = start_x - i * (digit_w + 10);
        draw_7segment_digit(bx, start_y, digit);
    }

    // Lives
    for (int i = 0; i < ship.lives; i++) {
        int lx = 40 + i * 45;
        SDL_SetRenderDrawColor(renderer,180, 255, 180, 255);
        thick_line(lx, 20, lx + 30, 20, 5);
        thick_line(lx + 8, 30, lx + 22, 30, 5);
    }

    // Fuel bar
    int fuel_fill = (int)((ship.fuel / 1000.0f) * 220);
    SDL_SetRenderDrawColor(renderer, 40, 60, 80, 220);
    SDL_RenderFillRect(renderer, &(SDL_Rect){30, 70, 240, 18});
    SDL_SetRenderDrawColor(renderer, 80, 200, 255, 255);
    SDL_RenderFillRect(renderer, &(SDL_Rect){33, 73, fuel_fill, 12});
    

    // Heat bar
    int heat_fill = (int)((ship.heat / OVERHEAT_MAX) * 220);
    SDL_SetRenderDrawColor(renderer, 40, 60, 80, 220);
    SDL_RenderFillRect(renderer, &(SDL_Rect){30, 70, 240, 18});
    if (is_critical_overheat()) {
        SDL_SetRenderDrawColor(renderer, 255, 60, 40, 255);
    } else if (is_overheat_warning()) {
        SDL_SetRenderDrawColor(renderer, 255, 140, 40, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 100, 80, 255);
    }
    SDL_RenderFillRect(renderer, &(SDL_Rect){33, 98, heat_fill, 8});

    // Combo meter
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

    // Wave indicator
    // Always show current wave number (bottom-right, subtle digits only)
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);  // Subtle gray-white
    int tx = WINDOW_W - 100;  // Bottom-right anchor for digits only
    int ty = WINDOW_H - 50;   // Bottom position
    int wave_digit_x = tx;    // Position digits directly
    draw_7segment_digit(wave_digit_x + 35, ty - 8, wave % 10);  // Right digit first for readability
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
#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

#define WINDOW_W 1200
#define WINDOW_H 675

#define SHIP_ROT_SPEED 0.10f
#define SHIP_THRUST 0.12f
#define OVERHEAT_MAX 300.0f

#define HEAT_GAIN_PER_THRUST 1.1f
#define HEAT_DECAY_NORMAL 0.7f
#define HEAT_DECAY_CRITICAL 0.35f
#define OVERHEAT_CRITICAL_THRESHOLD 1.00f
#define OVERHEAT_THRUST_PENALTY 0.30f
#define OVERHEAT_DRAG_MULTIPLIER 0.94f


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

Ship ship;

int frame = 0;

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;

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

bool is_critical_overheat() {
    return ship.heat >= OVERHEAT_MAX * OVERHEAT_CRITICAL_THRESHOLD;
}

void init_game() {
    srand(time(NULL));
    ship = (Ship) {
        WINDOW_W / 2.0f, WINDOW_H / 2.0f, 0, 0, -M_PI / 2,
        1000.0f, 0, 0, 0, 3, false, 0, false, 0,
        0.0f
    };
}

void update() {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    int left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    int right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    int thrust = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];

    frame++;

    if (left) ship.angle -= SHIP_ROT_SPEED;
    if (right) ship.angle += SHIP_ROT_SPEED;

    float effective_thrust = SHIP_THRUST;
    if (is_critical_overheat()) effective_thrust *= OVERHEAT_THRUST_PENALTY;

    if (thrust && ship.fuel > 5.0f) {
        ship.vx += cosf(ship.angle) * SHIP_THRUST;
        ship.vy += sinf(ship.angle) * SHIP_THRUST;
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
        ship.vy *- 0.985f;
        ship.overheat_damage_accumulator = fmaxf(0, ship.overheat_damage_accumulator - 0.4f);
    }


}

draw_ship() {
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
}

render() {
    SDL_SetRenderDrawColor(renderer, 3, 3, 12, 255);
    SDL_RenderClear(renderer);
    draw_ship();
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
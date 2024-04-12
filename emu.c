#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define WINDOW_W 1024
#define WINDOW_H 1024
#define W 256
#define H 256

typedef uint8_t bool_t;
#define TRUE 1
#define FALSE 0

// https://segaretro.org/Palette
// 64 colors available on Master System
static const uint8_t LUMINOSITY[] = { 0, 85, 170, 255 };
static uint32_t FULL_PALETTE[64];
void init_palette() {
	int i = 0;
	for (int g = 0; g < 4; g++) {
		for (int r = 0; r < 4; r++) {
			for (int b = 0; b < 4; b++) {
				FULL_PALETTE[i] =
					0xff000000 |
					LUMINOSITY[r] << 16 |
					LUMINOSITY[g] <<  8 |
					LUMINOSITY[b] <<  0;
				i += 1;
			}
		}
	}
}

static uint32_t pixels_argb[W*H];

#define SPRITE_DIM 8
const uint8_t SPRITE_DATA[][SPRITE_DIM] = {
	{	// alien frame 1
		0b00100100,
		0b00011000,
		0b00111100,
		0b01011010,
		0b11111111,
		0b10111101,
		0b10100101,
		0b00100100
	}, {// alien frame 2
		0b00100100,
		0b10011001,
		0b10111101,
		0b11011011,
		0b01111110,
		0b00111100,
		0b01000010,
		0b10000001
	}, {// player
		0b00011000,
		0b00011000,
		0b00111100,
		0b00111100,
		0b01111110,
		0b01111110,
		0b11111111,
		0b11111111,
	}, { // player shot
		0b00011000,
		0b00011000,
		0b00011000,
		0b00011000,
		0b00011000,
		0b00011000,
		0b00011000,
		0b00011000
	}, { // enemy shot
		0b00010000,
		0b00001000,
		0b00010000,
		0b00001000,
		0b00010000,
		0b00001000,
		0b00010000,
		0b00001000,
	}
};

typedef struct {
	uint8_t enable;
	uint8_t x;
	uint8_t y;
	uint8_t idx;
	uint8_t color;
} Sprite_t;


#define N_SPRITES 64

#define ENEMY_ROWS 5
#define ENEMY_COLS 11
#define N_ENEMIES (ENEMY_ROWS*ENEMY_COLS)
#define PLAYER_COLOR 18

static Sprite_t sprite[N_SPRITES];
#define SPRITE_PLAYER 63
#define SPRITE_PLAYER_SHOT 62
#define SPRITE_ENEMY_RETURN_SHOT 61
#define SPRITE_LIVES N_ENEMIES

#define SHOT_SPEED 4

static uint8_t joyport;
#define LEFT 1
#define RIGHT 2
#define BUTTON_A 16

static int lives;

typedef unsigned int Tick_t;
static Tick_t ticks;

#define N_TIMERS 4
typedef struct {
	Tick_t t;
	void (*callback)();
} Timer_t;
static Timer_t timer[N_TIMERS];
#define TIMER_RETURN_SHOT 0

static int enemies;
static int enemy_dir;
#define DIR_RIGHT 2
#define DIR_LEFT -2

void init_timers() {
	for (int i = 0; i < N_SPRITES; i++) {
		timer[i].t = 0;
	}
}
void init_sprites() {
	for (int i = 0; i < N_SPRITES; i++) {
		sprite[i].enable = FALSE;
	}
}

void init() {
	ticks = 0;
	init_timers();

	enemies = ENEMY_ROWS * ENEMY_COLS;
	enemy_dir = DIR_RIGHT;
	for (uint8_t j = 0; j < ENEMY_ROWS; j++) {
		for (uint8_t i = 0; i < ENEMY_COLS; i++) {
			Sprite_t* spr = &sprite[j*ENEMY_COLS+i];
			spr->enable = 1;
			spr->x = 16 + i*16;
			spr->y = 64 + j*16;
			spr->idx = 0;
			spr->color = j*8+20;
		}
	}
	lives = 3;
	Sprite_t* player = &sprite[SPRITE_PLAYER];
	player->enable = 1;
	player->x = 128;
	player->y = 248;
	player->color = PLAYER_COLOR;
	player->idx = 2;
}


static void update_enemies() {
	// Speed up as # enemies decreases/
	// In the original, only one enemy was moved per frame,
	// and the speed-up was a side-effect of that
	if (ticks % enemies == 0) {
		bool_t advance = FALSE;
		uint8_t xl = 255;
		uint8_t xr = 0;
		for (int i = 0 ; i < N_ENEMIES; i++) {
			if (sprite[i].enable) {
				xl = min(sprite[i].x, xl);
				xr = max(sprite[i].x, xr);
			}
		}
		if (enemy_dir == DIR_RIGHT && xr > (W - 2*SPRITE_DIM)) {
			enemy_dir = DIR_LEFT;
			advance = TRUE;
		} else if (enemy_dir == DIR_LEFT && xl < SPRITE_DIM) {
			enemy_dir = DIR_RIGHT;
			advance = TRUE;
		}
		for (uint8_t j = 0; j < ENEMY_ROWS; j++) {
			for (uint8_t i = 0; i < ENEMY_COLS; i++) {
				Sprite_t* spr = &sprite[j*ENEMY_COLS+i];
				spr->idx = (spr->idx + 1) % 2;

				if (advance) {
					spr->y += 2;
				} else {
					spr->x += enemy_dir;
				}
			}
		}
	}
}

static bool_t check_sprite_collision(int i, int j, int ir, int il) {
	const Sprite_t *si, *sj;
	si = &sprite[i];
	sj = &sprite[j];
	return (si->x+ir >= sj->x)
		&& (sj->x+SPRITE_DIM >= si->x+il)
		&& (si->y+SPRITE_DIM >= sj->y)
		&& (sj->y+SPRITE_DIM >= si->y);
}

static void update_player_shot() {
	if (sprite[SPRITE_PLAYER_SHOT].y < SHOT_SPEED) {
		sprite[SPRITE_PLAYER_SHOT].enable = FALSE;
	}
	sprite[SPRITE_PLAYER_SHOT].y -= SHOT_SPEED;

	for (int i = 0; i < N_ENEMIES; i++) {
		if (sprite[i].enable && check_sprite_collision(SPRITE_PLAYER_SHOT, i, 3, 5)) {
			sprite[i].enable = FALSE;
			sprite[SPRITE_PLAYER_SHOT].enable = FALSE;
			enemies--;
			// TODO: explosion
		}
	}
}

static int find_nearest_enemy_index() {
	int nearest_i = -1;
	int nearest_dx = 0xffff;
	for (int i = 0; i < N_ENEMIES; i++) {
		if (!sprite[i].enable) continue;
		int dx = abs(sprite[i].x - sprite[SPRITE_PLAYER].x);
		if (dx <= nearest_dx) {
			nearest_dx = dx;
			nearest_i = i;
		}
	}
	return nearest_i;
}

static void update_enemy_shots() {
	if (sprite[SPRITE_ENEMY_RETURN_SHOT].y >= H - SHOT_SPEED) {
		sprite[SPRITE_ENEMY_RETURN_SHOT].enable = FALSE;
	}
	sprite[SPRITE_ENEMY_RETURN_SHOT].y += SHOT_SPEED;

	if (check_sprite_collision(SPRITE_ENEMY_RETURN_SHOT, SPRITE_PLAYER, 3, 5)) {
		sprite[SPRITE_PLAYER].enable = FALSE;
		// TODO: explosion
		// decrement lives
	}
}

static void return_shot() {
	Sprite_t *spr = &sprite[SPRITE_ENEMY_RETURN_SHOT];
	spr->enable = TRUE;
	int i = find_nearest_enemy_index();
	spr->x = sprite[i].x;
	spr->y = sprite[i].y + SPRITE_DIM;
	spr->color = sprite[i].color;
	spr->idx = 4;
}

static void handle_inputs() {
	if (joyport & LEFT && sprite[SPRITE_PLAYER].x > 0) {
		sprite[SPRITE_PLAYER].x -= 1;
	}
	if (joyport & RIGHT && sprite[SPRITE_PLAYER].x < W - SPRITE_DIM) {
		sprite[SPRITE_PLAYER].x += 1;
	}
	if (joyport & BUTTON_A && !sprite[SPRITE_PLAYER_SHOT].enable) {
		sprite[SPRITE_PLAYER_SHOT].x = sprite[SPRITE_PLAYER].x;
		sprite[SPRITE_PLAYER_SHOT].y = 240;
		sprite[SPRITE_PLAYER_SHOT].idx = 3;
		sprite[SPRITE_PLAYER_SHOT].enable = TRUE;
		sprite[SPRITE_PLAYER_SHOT].color = PLAYER_COLOR;

		timer[TIMER_RETURN_SHOT].t = 20;
		timer[TIMER_RETURN_SHOT].callback = &return_shot;
	}
}

void handle_timers() {
	for (int i = 0; i < N_TIMERS; i++) {
		if (timer[i].t) {
			timer[i].t--;
			if (!timer[i].t) {
				timer[i].callback();
			}
		}
	}
}

static void update() {
	if (ticks % enemies == 0) { // speed up as # enemies decreases
		update_enemies();
	}
	if (sprite[SPRITE_PLAYER_SHOT].enable) {
		update_player_shot();
	}
	update_enemy_shots();

	handle_inputs();
	ticks++;
	handle_timers();
}

void draw_sprites() {
	for (int i = 0; i < N_SPRITES; i++) {
		if (sprite[i].enable) {
			const Sprite_t* spr = &sprite[i];
			const uint8_t* sprdata = &SPRITE_DATA[spr->idx][0];
			for (int y = 0; y < 8; y++) {
				uint8_t row = sprdata[y];
				for (int x = 0; x < 8; x++) {
					if (row & 0x80) {
						const int offset = (spr->y+y)*W + spr->x + x;
						if (offset < W*H) {
							pixels_argb[offset] = FULL_PALETTE[spr->color];
						}
					}
					row <<= 1;
				}
			}
		}
	}
}

SDL_GameController *findController() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            return SDL_GameControllerOpen(i);
        }
    }
    return 0;
}



// see https://benedicthenshaw.com/soft_render_sdl2.html
// (how to do software rendering in SDL2)
extern int main(int argc, char** argv) {
	SDL_Window* window = 0;
	SDL_Renderer* renderer = 0;
	SDL_Surface* surface = 0;
	SDL_Texture* texture = 0;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
		printf("Failed to initalize SDL: %s\n", SDL_GetError());
		return 0;
	}
	window = SDL_CreateWindow(
		"Fantasy console",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN
	);
	if (!window) {
		printf("Failed to create SDL window: %s\n", SDL_GetError());
		return 0;
	}
	// cap FPS to VSYNC
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		printf("Failed to create SDL renderer: %s\n", SDL_GetError());
		return 0;
	}
	SDL_RenderSetLogicalSize(renderer, W, H);
	SDL_RenderSetIntegerScale(renderer, 1);
	texture = SDL_CreateTexture(
		renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		W, H
	);

	if (SDL_GameControllerEventState(SDL_ENABLE) < 1) {
		printf("Failed to enable game controller events: %s\n", SDL_GetError());
		return 0;
	}
	SDL_GameController *controller = findController();

	init_palette();
	init_sprites();
	init();
	SDL_Event e;
	int quit = 0;
	while (!quit) {
		const uint64_t start = SDL_GetPerformanceCounter();
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				quit = 1;
				break;
			};
			if (e.type == SDL_CONTROLLERBUTTONDOWN) {
				switch (e.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
					joyport |= LEFT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					joyport |= RIGHT; break;
				case SDL_CONTROLLER_BUTTON_A:
					joyport |= BUTTON_A; break;
				}
			}
			if (e.type == SDL_CONTROLLERBUTTONUP) {
				switch (e.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
					joyport &= ~LEFT; break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					joyport &= ~RIGHT; break;
				case SDL_CONTROLLER_BUTTON_A:
					joyport &= ~BUTTON_A; break;
				}
			}
		}
		update();
		memset(pixels_argb, 0, W*H*sizeof(uint32_t));
		draw_sprites();

		SDL_RenderClear(renderer);
		SDL_UpdateTexture(texture, NULL, pixels_argb, W*4);
		SDL_RenderCopy(renderer, texture, 0, 0);
		SDL_RenderPresent(renderer);

		const uint64_t end = SDL_GetPerformanceCounter();
		const float elapsed = (float)(end - start) / (float)SDL_GetPerformanceFrequency();
		printf("%2.2f FPS \r", 1.0f/elapsed);
	}
	SDL_FreeSurface(surface);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

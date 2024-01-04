#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "SDL.h"
#include "SDL_ttf.h"
#include <math.h>

typedef double f64;
typedef float f32;

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

#define alloc(type, n) (calloc(n, sizeof(type)))
#define falloc(type, n) (malloc(n * sizeof(type)))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define PI 3.1415926535898

#define WIDTH 1280
#define HEIGHT 720

#define MOVEMENT_SPEED 1.5
#define ROTATION_SPEED 0.1 // mouse sensitivity

#define FRAMERATE_LIMIT 120

#define lerp(a, b, t) ((a) + (t) * ((b) - (a)))

#define assert(condition, ...)        \
    if (!(condition))                 \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    }

struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    u32 pixels[WIDTH * HEIGHT];
    bool quit;
} context;

static u8 map[8 * 8] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 3, 3, 0, 0, 1,
    1, 0, 0, 3, 3, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    6, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 2, 1, 1, 1, 1, 1};

struct
{
    f32 x;
    f32 y;
    f32 angle;
    f32 fov;
} camera;

// Lookup table for wall colors
static u32 wallColors[7] = {
    0x000000FF,
    0x0000FF00,
    0x00FF0000,
    0x00FFFFFF,
    0x00FFFF00};

// function to smoothly interpolate between values
static f64 interpolate(f64 a, f64 b, f64 x)
{
    f64 ft = x * PI;
    f64 f = (1 - cos(ft)) * 0.5;
    return a * (1 - f) + b * f;
}

bool checkCollision(i32 x, i32 y)
{
    if (x < 0 || x >= 8 || y < 0 || y >= 8)
    {
        return false;
    }
    return map[x + y * 8] == 0 || map[x + y * 8] == 6;
}

int main(int argc, char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0, "Failed to initialize SDL: %s\n", SDL_GetError());

    SDL_Window *window = SDL_CreateWindow("SDL Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, 0);
    assert(window != NULL, "Failed to create SDL window: %s\n", SDL_GetError());

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
    assert(renderer != NULL, "Failed to create SDL renderer: %s\n", SDL_GetError());

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    assert(texture != NULL, "Failed to create SDL texture: %s\n", SDL_GetError());

    const u8 *keys = SDL_GetKeyboardState(NULL);
    assert(keys != NULL, "Failed to get keyboard state: %s\n", SDL_GetError());

    // initialize the sdl2_ttf library
    assert(TTF_Init() == 0, "Failed to initialize SDL_ttf: %s\n", TTF_GetError());
    // load the font
    TTF_Font *font = TTF_OpenFont("font.ttf", 24);
    assert(font != NULL, "Failed to load font: %s\n", TTF_GetError());

    // Initialize camera by setting the initial position to "5" in the map
    for (int i = 0; i < 8 * 8; i++)
    {
        if (map[i] == 6)
        {
            camera.x = (i % 8) + 0.5;
            camera.y = (i / 8) + 0.5;
            break;
        }
    }

    // Pre-calculate sine and cosine values
    f64 cosAngle = cos(camera.angle);
    f64 sinAngle = sin(camera.angle);

    // Main loop
    u32 prevTime = SDL_GetTicks();
    bool quit = false;
    while (!quit)
    {
        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                quit = true;
                break;
            }
        }

        // Calculate delta time
        u32 currentTime = SDL_GetTicks();
        f64 deltaTime = (currentTime - prevTime) / 1000.0;
        prevTime = currentTime;

        // clear the screen
        u32 pixels[WIDTH * HEIGHT] = {0};

        // raycaster, let's first place the player in the middle of the map, and cast rays
        for (int x = 0; x < WIDTH; x++)
        {
            // calculate ray position and direction
            f64 cameraX = 2 * x / (f64)WIDTH - 1; // x-coordinate in camera space
            f64 rayDirX = cosAngle + sinAngle * cameraX;
            f64 rayDirY = sinAngle - cosAngle * cameraX;

            // which box of the map we're in
            i32 mapX = (i32)camera.x;
            i32 mapY = (i32)camera.y;

            // length of ray from current position to next x or y-side
            f64 sideDistX;
            f64 sideDistY;

            // length of ray from one x or y-side to next x or y-side
            f64 deltaDistX = sqrt(1 + (rayDirY * rayDirY) / (rayDirX * rayDirX));
            f64 deltaDistY = sqrt(1 + (rayDirX * rayDirX) / (rayDirY * rayDirY));
            f64 perpWallDist;

            // what direction to step in x or y-direction (either +1 or -1)
            i32 stepX;
            i32 stepY;

            i32 hit = 0; // was there a wall hit?
            i32 side;    // was a NS or a EW wall hit?

            // calculate step and initial sideDist
            if (rayDirX < 0)
            {
                stepX = -1;
                sideDistX = (camera.x - mapX) * deltaDistX;
            }
            else
            {
                stepX = 1;
                sideDistX = (mapX + 1.0 - camera.x) * deltaDistX;
            }
            if (rayDirY < 0)
            {
                stepY = -1;
                sideDistY = (camera.y - mapY) * deltaDistY;
            }
            else
            {
                stepY = 1;
                sideDistY = (mapY + 1.0 - camera.y) * deltaDistY;
            }

            // perform DDA
            while (hit == 0)
            {
                // jump to next map square, OR in x-direction, OR in y-direction
                if (sideDistX < sideDistY)
                {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                }
                else
                {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                // Check if ray has hit a wall
                if (mapX < 0 || mapX >= 8 || mapY < 0 || mapY >= 8 || map[mapX + mapY * 8] > 0 && map[mapX + mapY * 8] != 6)
                {
                    hit = 1;
                }
            }

            // Calculate distance projected on camera direction (oblique distance will give fisheye effect!)
            if (side == 0)
                perpWallDist = (mapX - camera.x + (1 - stepX) / 2) / rayDirX;
            else
                perpWallDist = (mapY - camera.y + (1 - stepY) / 2) / rayDirY;

            // Calculate height of line to draw on screen
            i32 lineHeight = (i32)(HEIGHT / perpWallDist);

            // calculate lowest and highest pixel to fill in current stripe
            i32 drawStart = -lineHeight / 2 + HEIGHT / 2;
            if (drawStart < 0)
                drawStart = 0;
            i32 drawEnd = lineHeight / 2 + HEIGHT / 2;
            if (drawEnd >= HEIGHT)
                drawEnd = HEIGHT - 1;

            // choose wall color
            u32 color = wallColors[map[mapX + mapY * 8]];

            // darken the shade of the color depending on the perpendicular distance
            f64 shade = 1.0 - (perpWallDist / 10.0);
            shade = (shade < 0.0) ? 0.0 : shade;

            // make y-sides darker
            if (side == 1)
            {
                shade *= 0.5;
            }
            else if (side == 0)
            {
                shade *= 0.75;
            }

            // make the color darker
            color = (u32)((f64)((color >> 24) & 0xFF) * shade) << 24 | (u32)((f64)((color >> 16) & 0xFF) * shade) << 16 | (u32)((f64)((color >> 8) & 0xFF) * shade) << 8 | (u32)((f64)((color >> 0) & 0xFF) * shade) << 0;

            // draw the pixels of the stripe as a vertical line
            for (int y = drawStart; y < drawEnd; y++)
            {
                pixels[x + y * WIDTH] = color;
            }
        }

        // handle input
        f64 moveSpeed = MOVEMENT_SPEED * deltaTime;
        f64 rotationSpeed = ROTATION_SPEED * deltaTime;

        // if shift is being held down, move faster
        if (keys[SDL_SCANCODE_LSHIFT])
        {
            moveSpeed *= 1.2;
        }

        if (keys[SDL_SCANCODE_W])
        {
            f64 newX = camera.x + (cosAngle * moveSpeed);
            f64 newY = camera.y + (sinAngle * moveSpeed);
            if (checkCollision((i32)newX, (i32)newY))
            {
                camera.x = newX;
                camera.y = newY;
            }
        }

        if (keys[SDL_SCANCODE_S])
        {
            f64 newX = camera.x - (cosAngle * moveSpeed);
            f64 newY = camera.y - (sinAngle * moveSpeed);
            if (checkCollision((i32)newX, (i32)newY))
            {
                camera.x = newX;
                camera.y = newY;
            }
        }

        if (keys[SDL_SCANCODE_A])
        {
            f64 newX = camera.x - (sinAngle * moveSpeed);
            f64 newY = camera.y + (cosAngle * moveSpeed);
            if (checkCollision((i32)newX, (i32)newY))
            {
                camera.x = newX;
                camera.y = newY;
            }
        }

        if (keys[SDL_SCANCODE_D])
        {
            f64 newX = camera.x + (sinAngle * moveSpeed);
            f64 newY = camera.y - (cosAngle * moveSpeed);
            if (checkCollision((i32)newX, (i32)newY))
            {
                camera.x = newX;
                camera.y = newY;
            }
        }

        i32 mouseX, mouseY;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);
        camera.angle += mouseX * rotationSpeed;

        // calculate sine and cosine values
        cosAngle = cos(camera.angle);
        sinAngle = -sin(camera.angle);

        // hide the mouse and lock it to the window
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_WarpMouseInWindow(window, WIDTH / 2, HEIGHT / 2);
        SDL_ShowCursor(SDL_DISABLE);

        // framerate limit
        u32 frameTime = SDL_GetTicks() - currentTime;
        if (frameTime < 1000 / FRAMERATE_LIMIT)
        {
            SDL_Delay(1000 / FRAMERATE_LIMIT - frameTime);
        }

        // Update texture
        SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(u32));

        // Render
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);

        // Render debug info to the screen
        SDL_Color color = {255, 255, 255, 255};
        char buffer[256];
        sprintf(buffer, "FPS: %3d", (int)(1.0 / deltaTime));
        SDL_Surface *surface = TTF_RenderText_Solid(font, buffer, color);
        SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect rect = {10, 10, surface->w, surface->h};
        SDL_RenderCopy(renderer, textTexture, NULL, &rect);
        SDL_DestroyTexture(textTexture);
        SDL_FreeSurface(surface);
        sprintf(buffer, "X: %.2f, Y: %.2f, Angle: %.2f", camera.x, camera.y, camera.angle);
        surface = TTF_RenderText_Solid(font, buffer, color);
        textTexture = SDL_CreateTextureFromSurface(renderer, surface);
        rect = (SDL_Rect){10, 40, surface->w, surface->h};

        // draw a crosshair in the middle of the screen
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, WIDTH / 2 - 5, HEIGHT / 2, WIDTH / 2 + 5, HEIGHT / 2);
        SDL_RenderDrawLine(renderer, WIDTH / 2, HEIGHT / 2 - 5, WIDTH / 2, HEIGHT / 2 + 5);

        SDL_RenderCopy(renderer, textTexture, NULL, &rect);
        SDL_DestroyTexture(textTexture);
        SDL_FreeSurface(surface);

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();

    return 0;
}

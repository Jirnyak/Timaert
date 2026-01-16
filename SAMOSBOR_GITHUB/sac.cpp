#define SDL_MAIN_HANDLED

#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <unistd.h>
#include <string>
#include <math.h>   
#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <set>
#include <cstring>   // std::memset
#include <cctype>    // std::isdigit

//FOR WINDOWS
#include <array>
#include <unordered_map>

using namespace std;

#define WORLD_WIDTH 1024
#define MAX_OBJECTS 128 //square is max

#define TILE_SIZE 16

#define PI 3.1415926535

enum mod
{
    GEN,
    EXIT,
    GAME,
    MENU,
    STAT,
    MAP,
    LOAD,
    EVENT,
    FIGHT
};

enum state
{
    DEFAULT
};

/*

cd /Users/jirnyak/Mirror/SVYATO

./sac

COMPILER DEBUG

-g -O0 

lldb ./sac

run

exit

-O3 -ffast-math 


*/

#include "tergen.h"
#include "input.h"
#include "objects.h"

const char* menu_items[3] = {
    "New Game",
    "Load",
    "Exit"
};

using rng_t = mt19937;

random_device dev;

mt19937 rng(dev());

uint32_t randomer(rng_t& rng, uint32_t range) 
{
    range += 1;
    uint32_t x = rng();
    uint64_t m = uint64_t(x) * uint64_t(range);
    uint32_t l = uint32_t(m);
    if (l < range) {
        uint32_t t = -range;
        if (t >= range) {
            t -= range;
            if (t >= range) 
                t %= range;
        }
        while (l < t) {
            x = rng();
            m = uint64_t(x) * uint64_t(range);
            l = uint32_t(m);
        }
    }
    return m >> 32;
}

class faction
{
    public:
    unsigned char number;
    unsigned char R;
    unsigned char G;
    unsigned char B;

    faction(int number)
    {
        this->number = number;
        this->R = randomer(rng,255);
        this->G = randomer(rng,255);
        this->B = randomer(rng,255);
    } 
};

class cell 
{
    private:
        short int x;  
        short int y;  

        cell *sosed_up;   
        cell *sosed_left; 
        cell *sosed_down; 
        cell *sosed_right; 

    public:  

        cell() : x(0), y(0), sosed_up(nullptr), sosed_left(nullptr), sosed_down(nullptr), sosed_right(nullptr) {}

        cell(int x, int y) 
        { 
            this->x = x;
            this->y = y;
        }
        void up(cell *c)
        {
            sosed_up = c;
        }
        void down(cell *c)
        {
            sosed_down = c;
        }
        void left(cell *c)
        {
            sosed_left = c;
        }
        void right(cell *c)
        {
            sosed_right = c;
        }
        cell* side(int d)
        {
            if (d == 0)
                return sosed_up;
            if (d == 1)
                return sosed_left;
            if (d == 2)
                return sosed_down;
            if (d == 3)
                return sosed_right;
            else
                exit(1);
        }
        int get_x() const
        {
            return x;
        }
        int get_y() const
        {
            return y;
        }
        int get_n(int razmer = WORLD_WIDTH) const
        {
            return x*razmer+ y;
        }
};

struct mapo
{
    unsigned char R;
    unsigned char G;
    unsigned char B;
};

int tor_cord(int x, int razmer = WORLD_WIDTH)
{
    if (x < 0)
    {
        x=(razmer+x)%razmer;
    }
    else if (x >= razmer)
    {
        x = x%razmer;
    }
    return x;
}

int rasstoyanie(int x1, int y1, int x2, int y2)
{
    int dx = abs(x1 - x2);
    if (dx > WORLD_WIDTH / 2) 
    {
        dx = WORLD_WIDTH - dx;
    }
    int dy = abs(y1 - y2);
    if (dy > WORLD_WIDTH / 2) 
    {
        dy = WORLD_WIDTH - dy;
    }
    return sqrt(dx*dx + dy*dy);
}

void render_text(SDL_Renderer* renderer,  TTF_Font* font, const std::string& text, int x, int y, int width, int height, const SDL_Color& color) 
{
    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    SDL_Rect rect = { x, y, width, height};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_DestroyTexture(texture);
}

SDL_Texture* img_mapo(SDL_Renderer* renderer,SDL_Texture* texture,const mapo* pixels,int N)
{
    if (!texture) {
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            N, N
        );
        if (!texture) return nullptr;
    }

    void* texPixels = nullptr;
    int pitch = 0;

    if (SDL_LockTexture(texture, nullptr, &texPixels, &pitch) != 0)
        return texture;

    SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);

    for (int y = 0; y < N; ++y) {
        Uint32* row = (Uint32*)((Uint8*)texPixels + y * pitch);
        const mapo* src = pixels + y * N;

        for (int x = 0; x < N; ++x) {
            row[x] = SDL_MapRGB(fmt,
                                src[x].R,
                                src[x].G,
                                src[x].B);
        }
    }

    SDL_FreeFormat(fmt);
    SDL_UnlockTexture(texture);
    return texture;
}

template<typename T>
void save_array(const std::string &filename, const T* arr, size_t size) 
{
    std::ofstream out(filename, std::ios::binary);
    if (!out) return;
    out.write(reinterpret_cast<const char*>(arr), sizeof(T) * size);
}

template<typename T>
void load_array(const std::string &filename, T* arr, size_t size) 
{
    std::ifstream in(filename, std::ios::binary);
    if (!in) return;
    in.read(reinterpret_cast<char*>(arr), sizeof(T) * size);
}

int main(int argc, char **argv) 
{
    default_random_engine generator;
    normal_distribution<double> distribution(1, 0.1);
    normal_distribution<double> evolve(0, 0.01);

    vector<cell> world;
    vector<cell>::iterator it;

    for (int i=0; i<WORLD_WIDTH; ++i)
    {
        for (int j=0; j<WORLD_WIDTH; ++j)
        {       
            world.push_back(cell(i,j));
        }
    }

    for (it = world.begin(); it != world.end(); ++it)
    {
        it->up(&world[tor_cord(it->get_x())*WORLD_WIDTH + tor_cord(it->get_y()-1)]);
        it->down(&world[tor_cord(it->get_x())*WORLD_WIDTH + tor_cord(it->get_y()+1)]);
        it->left(&world[tor_cord(it->get_x()-1)*WORLD_WIDTH + tor_cord(it->get_y())]);
        it->right(&world[tor_cord(it->get_x()+1)*WORLD_WIDTH + tor_cord(it->get_y())]);
    }

    //GAME STATES
    bool fullscreen = 1;
    bool paused = 1;
    bool quit = 0;
    bool freecam = 1;
    int game_mod = MENU;
    bool picked = 0;
    bool screenshot = 0;

    //WORKVAR
    int drop;
    int drop1;
    int checker;

    SDL_Event event;
    SDL_Renderer *renderer;
    SDL_Window *window;
    SDL_Init(SDL_INIT_VIDEO);

    SDL_DisplayMode current;
    for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i)
    {
        SDL_GetDesktopDisplayMode(i, &current);
    }

    int WINDOW_WIDTH = current.w;
    int WINDOW_HEIGHT = current.h;

    int SCREEN_CENTER_X = WINDOW_WIDTH  / 2;
    int SCREEN_CENTER_Y = WINDOW_HEIGHT / 2;


    SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);
    SDL_SetWindowFullscreen(window, 0);
    //SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);


//TEXT
    TTF_Init();
    TTF_Font *font = TTF_OpenFont("Roboto-Black.ttf", 20);

    string text;
    char input[64];

    IMG_Init(IMG_INIT_PNG);

    #include "textures.h"

    SDL_Rect tile;
    tile.w = TILE_SIZE;
    tile.h = TILE_SIZE;
    tile.x = 0;
    tile.y = 0;

    SDL_Rect ui;
    ui.w = 0;
    ui.h = 0;
    ui.x = 0;
    ui.y = 0;

    int box_x, box_y;

    int curs_x;
    int curs_y;

    int pick_x;
    int pick_y;
    int frame = SDL_GetTicks();

    int cam_x = WORLD_WIDTH/2;
    int cam_y = WORLD_WIDTH/2;

    int pos_cam = cam_x*WORLD_WIDTH + cam_y;
    cell* pos = &world[pos_cam];
    cell* pos_line = pos;

    unsigned long int hour = 0;
    unsigned int seed;

    //game arrays
    char* relief = new char[WORLD_WIDTH*WORLD_WIDTH];
    fill(relief, relief + (WORLD_WIDTH * WORLD_WIDTH), NOTHING);
    char* owner = new char[WORLD_WIDTH*WORLD_WIDTH];
    fill(owner, owner + (WORLD_WIDTH * WORLD_WIDTH), NOTHING);

    mapo world_map[WORLD_WIDTH*WORLD_WIDTH];
    SDL_Texture* world_image = nullptr;

    //tergen arrays
    float* field = new float[WORLD_WIDTH*WORLD_WIDTH];
    float* temp  = new float[WORLD_WIDTH*WORLD_WIDTH];

    //Objects map
    std::unordered_map<int, std::vector<int>> pos_map;

    while (quit == false) 
    {
        frame = SDL_GetTicks();
        SDL_GetMouseState(&curs_x, &curs_y);

        ui.w = 500;
        ui.h = 300;
        ui.x = 0;
        ui.y = WINDOW_HEIGHT-300;

        switch(game_mod)
        {
            case MENU:
                #include "menu.h"
                break;
            case GEN:
                #include "gen.h"
                break;
            case GAME:
                #include "game.h"
                break;
            case MAP:
                #include "map.h"
                break;
            case STAT:
                //#include "stat.h"
                break;
            case EVENT:
               // #include "event.h"
                break;
            case FIGHT:
                //#include "fight.h"ss
                break;
            case LOAD:
                #include "load.h"
                break;

        }
        SDL_RenderPresent(renderer);

        //OBJECTS POSITIONS
        pos_map.clear();
        for (int id = 0; id < MAX_OBJECTS*MAX_OBJECTS; ++id) 
        {
            const entity& e = objects[id];
            if (!e.active) continue;
        
            pos_map[e.pos].push_back(id);
        }


//SCRENNSHOT

        if (screenshot == 1)
        {
            SDL_Surface* shot = SDL_CreateRGBSurface(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, 0, 0, 0, 0);
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
            string pngFilename = "save.png";
            SDL_SaveBMP(shot, pngFilename.c_str());
            screenshot = 0;
        }
    }

            // --- LOGGING: entities ---
for (const auto& pair : pos_map)
{
    int pos = pair.first;
    const std::vector<int>& ids = pair.second;

    std::cout << "Position " << pos << " has entities: ";
    for (int eid : ids)
        std::cout << eid << " ";
    std::cout << "\n";
}


    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
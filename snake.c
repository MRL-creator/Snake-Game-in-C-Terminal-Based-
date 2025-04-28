#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Game constants
#define WIDTH 40
#define HEIGHT 20
#define INITIAL_SNAKE_LENGTH 3
#define SPEED_INCREASE_INTERVAL 3  // Speed increases after eating this many food items
#define SPEED_INCREASE_PERCENT 5  // Speed increases by this percentage

// Game elements
#define EMPTY ' '
#define SNAKE_BODY 'o'
#define SNAKE_HEAD '@'
#define FOOD '*'
#define WALL '#'

// ANSI escape codes for terminal control
#define ANSI_CLEAR_SCREEN "\033[2J"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"
#define ANSI_RESET_CURSOR "\033[H"
#define ANSI_GOTO_POS "\033[%d;%dH"

// ANSI color codes
#define ANSI_COLOR_RED     "\033[31m"
#define ANSI_COLOR_GREEN   "\033[32m"
#define ANSI_COLOR_YELLOW  "\033[33m"
#define ANSI_COLOR_RESET   "\033[0m"

// Direction types
typedef enum{
    UP, DOWN, LEFT, RIGHT
} Direction;

// Position structure
typedef struct{
    int x;
    int y;
} Position;

// Snake structure
typedef struct{
    Position positions[WIDTH * HEIGHT]; // Max possible snake length
    int length;
    Direction direction;
    Direction pending_direction;  // Store the next direction change
    bool direction_changed;       // Flag to track if direction was already changed this frame
} Snake;

// Game state
typedef struct{
    char current_grid[HEIGHT][WIDTH];
    char previous_grid[HEIGHT][WIDTH];
    Snake snake;
    Position food;
    int score;
    int prev_score;
    bool game_over;
    int base_speed_h;    // Base horizontal speed
    int base_speed_v;    // Base vertical speed
    int current_speed_h; // Current horizontal speed (affected by difficulty)
    int current_speed_v; // Current vertical speed (affected by difficulty)
    int food_eaten_since_speedup; // Track food eaten since last speed increase
    int difficulty_level; // Current difficulty level
} GameState;

void initializeGame(GameState *game);
void generateFood(GameState *game);
void updateGameState(GameState *game);
void renderGame(GameState *game, bool force_full_render);
void moveSnake(GameState *game);
void handleInput(GameState *game, char input);
bool checkCollision(GameState *game);
void cleanupGame(GameState *game);
void increaseGameSpeed(GameState *game);
unsigned long getCurrentTimeMs(); // unsigned long is used to avoid overflow issues
void setupTerminal(); 
void restoreTerminal(); 
void moveCursor(int y, int x);

#ifndef _WIN32
struct termios orig_termios;

// For kbhit() and getch() functions on Unix-like systems 
int kbhit(void)
{
    struct termios oldt, newt;
    int ch;
    int oldf;
    
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    
    
    if(ch != EOF){
        ungetc(ch, stdin);
        return 1;
    }
    
    return 0;
}

int getch(void)
{
    int ch;
    struct termios oldt, newt;
    
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    
    return ch;
}

void setupTerminal()
{
    // Save current terminal settings
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    // Configure terminal for game (non-canonical mode, no echo)
    struct termios new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    // Hide cursor and clear screen
    printf(ANSI_HIDE_CURSOR);
    printf(ANSI_CLEAR_SCREEN);
    printf(ANSI_RESET_CURSOR);
    fflush(stdout);
}

void restoreTerminal()
{
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    
    // Show cursor
    printf(ANSI_SHOW_CURSOR);
    fflush(stdout);
}
#else

void setupTerminal()
{
    system("cls"); // Clear the console screen in Windows
    
    // Get and set console info to hide cursor in Windows
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &info);
}

void restoreTerminal()
{
    // Show cursor in Windows
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = TRUE;
    SetConsoleCursorInfo(consoleHandle, &info);
}
#endif

void moveCursor(int y, int x)
{
#ifdef _WIN32
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
#else
    printf(ANSI_GOTO_POS, y + 1, x + 1);  // ANSI is 1-based
    fflush(stdout);
#endif
}

// Get current time in milliseconds for timing purposes
unsigned long getCurrentTimeMs()
{
#ifdef _WIN32
    SYSTEMTIME time;
    GetSystemTime(&time);
    return (time.wSecond * 1000) + time.wMilliseconds;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif
}

void sleep_ms(int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

void initializeGame(GameState *game)
{
    // Initialize game state
    game->score = 0;
    game->prev_score = 0;
    game->game_over = false;
    game->food_eaten_since_speedup = 0;
    game->difficulty_level = 1;
    
    game->base_speed_h = 160;       // Starting speed(horizontal speed)
    game->base_speed_v = 160;       // Starting speed(vertical speed)
    game->current_speed_h = game->base_speed_h; // Current speed (horizontal)
    game->current_speed_v = game->base_speed_v; // Current speed (vertical)
    
    // Initialize grids
    for (int y = 0; y < HEIGHT; y++){
        for (int x = 0; x < WIDTH; x++){
            game->current_grid[y][x] = EMPTY;
            game->previous_grid[y][x] = EMPTY;
        }
    }
    
    game->snake.length = INITIAL_SNAKE_LENGTH;
    game->snake.direction = RIGHT;
    game->snake.pending_direction = RIGHT;
    game->snake.direction_changed = false;
    
    // Centers the snake in the middle of the screen
    int middle_x = WIDTH / 2;
    int middle_y = HEIGHT / 2;
    
    for (int i = 0; i < game->snake.length; i++){
        game->snake.positions[i].x = middle_x - i;
        game->snake.positions[i].y = middle_y;
    }
    
    generateFood(game);
    updateGameState(game);
}

void generateFood(GameState *game)
{
    Position empty_cells[WIDTH * HEIGHT]; // Find empty cells
    int num_empty_cells = 0;
    
    for (int y = 1; y < HEIGHT - 1; y++){  // Skip border rows
        for (int x = 1; x < WIDTH - 1; x++){  // Skip border columns
            bool is_empty = true;
            
            // Check if cell is occupied by snake
            for (int i = 0; i < game->snake.length; i++){
                if (game->snake.positions[i].x == x && game->snake.positions[i].y == y){
                    is_empty = false;
                    break;
                }
            }
            
            if (is_empty){
                empty_cells[num_empty_cells].x = x;
                empty_cells[num_empty_cells].y = y;
                num_empty_cells++;
            }
        }
    }
    
    // Randomly select an empty cell for food
    if (num_empty_cells > 0){
        int random_index = rand() % num_empty_cells;
        game->food = empty_cells[random_index];
    }
}

void updateGameState(GameState *game)
{
    // Save previous grid state
    memcpy(game->previous_grid, game->current_grid, sizeof(game->current_grid));
    
    // Clear current grid
    for (int y = 0; y < HEIGHT; y++){
        for (int x = 0; x < WIDTH; x++){
            game->current_grid[y][x] = EMPTY;
        }
    }
    
    // Borders
    for (int x = 0; x < WIDTH; x++){
        game->current_grid[0][x] = WALL;
        game->current_grid[HEIGHT - 1][x] = WALL;
    }
    
    for (int y = 0; y < HEIGHT; y++){
        game->current_grid[y][0] = WALL;
        game->current_grid[y][WIDTH - 1] = WALL;
    }
    
    // Creation fo snake
    for (int i = 0; i < game->snake.length; i++){
        int x = game->snake.positions[i].x;
        int y = game->snake.positions[i].y;
        
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT){
            game->current_grid[y][x] = (i == 0) ? SNAKE_HEAD : SNAKE_BODY;
        }
    }
    
    // Food is always added to the grid randomly
    game->current_grid[game->food.y][game->food.x] = FOOD;
}



void renderGame(GameState *game, bool force_full_render)
{
    // Update score if changed
    if (game->score != game->prev_score || force_full_render){
        moveCursor(0, 0);
        printf("Score: %d | Level: %d         ", game->score, game->difficulty_level);
        game->prev_score = game->score;
    }
    
    // Render the game grid
    if (force_full_render){
        // Full redraw
        moveCursor(1, 0);
        for (int y = 0; y < HEIGHT; y++){
            for (int x = 0; x < WIDTH; x++){
                char cell = game->current_grid[y][x];
                switch (cell){
                    case WALL:
                        printf("%s%c%s", ANSI_COLOR_RED, cell, ANSI_COLOR_RESET);
                        break;
                    case FOOD:
                        printf("%s%c%s", ANSI_COLOR_GREEN, cell, ANSI_COLOR_RESET);
                        break;
                    case SNAKE_HEAD:
                    case SNAKE_BODY:
                        printf("%s%c%s", ANSI_COLOR_YELLOW, cell, ANSI_COLOR_RESET);
                        break;
                    default:
                        putchar(cell);
                        break;
                }
            }
            putchar('\n');
        }
    } else{
        // Only the changed cells are updated
        for (int y = 0; y < HEIGHT; y++){
            for (int x = 0; x < WIDTH; x++){
                if (game->current_grid[y][x] != game->previous_grid[y][x]){
                    moveCursor(y + 1, x);  // +1 for score line
                    char cell = game->current_grid[y][x];
                    switch (cell){
                        case WALL:
                            printf("%s%c%s", ANSI_COLOR_RED, cell, ANSI_COLOR_RESET);
                            break;
                        case FOOD:
                            printf("%s%c%s", ANSI_COLOR_GREEN, cell, ANSI_COLOR_RESET);
                            break;
                        case SNAKE_HEAD:
                        case SNAKE_BODY:
                            printf("%s%c%s", ANSI_COLOR_YELLOW, cell, ANSI_COLOR_RESET);
                            break;
                        default:
                            putchar(cell);
                            break;
                    }
                }
            }
        }
    }
    
    // Display instructions
    if (force_full_render){
        moveCursor(HEIGHT + 1, 0);
        printf("Controls: W/^ (Up), A/< (Left), S/v (Down), D/> (Right), Q (Quit)");
    }
    
    fflush(stdout);
}

void increaseGameSpeed(GameState *game) 
{

    game->difficulty_level++; // Increase difficulty level
    
    int base_speed = (game->base_speed_h + game->base_speed_v) / 2; // Use the same base speed for both horizontal and vertical movement for balance
    
    // Gradually reduce the delay to increase game speed by 5% per level
    int new_speed = base_speed * (100 - (game->difficulty_level - 1) * SPEED_INCREASE_PERCENT) / 100;
    
    // Horizontal and vertical speed are set to the same value (new_speed is the average speed)   
    game->current_speed_h = new_speed;
    game->current_speed_v = new_speed;
    
    // Minimum speed limit 40 ms
    if (game->current_speed_h < 40) game->current_speed_h = 40;
    if (game->current_speed_v < 40) game->current_speed_v = 40;
    
    game->food_eaten_since_speedup = 0;  // Reset the food counter to start tracking the number of food items consumed by the snake
}

void moveSnake(GameState *game)
{
    // Direction change logic - only change direction if it was changed this frame
    game->snake.direction = game->snake.pending_direction;
    game->snake.direction_changed = false;
    
    //  Store the last tail position
    Position last_tail = game->snake.positions[game->snake.length - 1];
    
    // Move the snake's body
    for (int i = game->snake.length - 1; i > 0; i--){
        game->snake.positions[i] = game->snake.positions[i - 1];
    }
    
    // Snakes head moves in the current direction 
    switch (game->snake.direction){
        case UP:
            game->snake.positions[0].y--;
            break;
        case DOWN:
            game->snake.positions[0].y++;
            break;
        case LEFT:
            game->snake.positions[0].x--;
            break;
        case RIGHT:
            game->snake.positions[0].x++;
            break;
    }
    
    // Check if snake ate food
    if (game->snake.positions[0].x == game->food.x && game->snake.positions[0].y == game->food.y){
        // Increase snake length
        game->snake.length++;
        game->snake.positions[game->snake.length - 1] = last_tail;
        
        game->score++;
        
        // Update food counter and check if speed should increase
        game->food_eaten_since_speedup++;
        if (game->food_eaten_since_speedup >= SPEED_INCREASE_INTERVAL){
            increaseGameSpeed(game);
        }
        
        // Generate new food
        generateFood(game);
    }

    game->game_over = checkCollision(game);
}

void handleInput(GameState *game, char input)
{
    input = toupper(input);
    
    
    if (!game->snake.direction_changed){ // Check if direction was already changed this frame
        // Handle directional input
        switch (input){
            case 'W': // Up
                if (game->snake.direction != DOWN && game->snake.direction != UP){ // To prevent immediate reversal
                    game->snake.pending_direction = UP;
                    game->snake.direction_changed = true;
                }
                break;
            case 'A': // Left
                if (game->snake.direction != RIGHT && game->snake.direction != LEFT){ // To prevent immediate reversal
                    game->snake.pending_direction = LEFT;
                    game->snake.direction_changed = true;
                }
                break;
            case 'S': // Down
                if (game->snake.direction != UP && game->snake.direction != DOWN){ // To prevent immediate reversal
                    game->snake.pending_direction = DOWN;
                    game->snake.direction_changed = true;
                }
                break;
            case 'D': // Right
                if (game->snake.direction != LEFT && game->snake.direction != RIGHT){ // To prevent immediate reversal
                    game->snake.pending_direction = RIGHT;
                    game->snake.direction_changed = true;
                }
                break;
            case 'Q': // Quit
                game->game_over = true;
                break;
        }
    }
}

bool checkCollision(GameState *game){
    // Get head position
    int head_x = game->snake.positions[0].x;
    int head_y = game->snake.positions[0].y;
    
    // Check wall collisions
    if (head_x <= 0 || head_x >= WIDTH - 1 || head_y <= 0 || head_y >= HEIGHT - 1){
        return true;
    }
    
    // Check self-collision
    for (int i = 1; i < game->snake.length; i++){
        if (head_x == game->snake.positions[i].x && head_y == game->snake.positions[i].y){
            return true;
        }
    }
    
    return false;
}

void cleanupGame(GameState *game)
{
    // Clear screen
    moveCursor(0, 0);
    printf(ANSI_CLEAR_SCREEN);
    printf(ANSI_RESET_CURSOR);
    
    // Display game over message
    printf("Game Over!\n");
    printf("Final Score: %d\n", game->score);
    printf("Difficulty Level: %d\n", game->difficulty_level);
    printf("Press any key to exit...\n");
    fflush(stdout);
    
    getch();
}

int main()
{
    // Seed random number generator
    srand(time(NULL));
    
    // Setup terminal for the game
    setupTerminal();
    
    // Initialize game state
    GameState game;
    initializeGame(&game);
    
    // Initial full render
    updateGameState(&game);
    renderGame(&game, true);
    
    // Game loop variables
    unsigned long last_move_time = getCurrentTimeMs();
    
    // Game loop
    while (!game.game_over){
        // Handle input
        if (kbhit()){
            char input = getch();
            handleInput(&game, input);
        }
        
        // Get current time
        unsigned long current_time = getCurrentTimeMs();
        
        // Move snake based on the appropriate speed for current direction
        int speed = (game.snake.direction == LEFT || game.snake.direction == RIGHT) 
                    ? game.current_speed_h : game.current_speed_v;
                    
        if (current_time - last_move_time >= speed){
            moveSnake(&game);
            updateGameState(&game);
            renderGame(&game, false);  // Incremental update
            last_move_time = current_time;
        }
        
        // Small delay to prevent CPU overload
        sleep_ms(16);  // ~60 FPS for input handling
    }
    
    // Restore terminal and show game over
    restoreTerminal();
    cleanupGame(&game);
    
    return 0;
}
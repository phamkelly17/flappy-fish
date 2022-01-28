// compile with: clang++ -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3 -o team12-flappyfish team12-flappyfish.cpp
// run with: ./fishies 2> /dev/null
// run with: ./fishies 2> debugoutput.txt
//  "2>" redirect standard error (STDERR; cerr)
//  /dev/null is a "virtual file" which discard contents

// Works best in Visual Studio Code if you set:
//   Settings -> Features -> Terminal -> Local Echo Latency Threshold = -1

// https://en.wikipedia.org/wiki/ANSI_escape_code#3-bit_and_4-bit

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono> // for dealing with time intervals
#include <cmath> // for max() and min()
#include <termios.h> // to control terminal modes
#include <unistd.h> // for read()
#include <fcntl.h> // to enable / disable non-blocking read()

// Because we are only using #includes from the standard, names shouldn't conflict
using namespace std;

// Constants

// Disable JUST this warning (in case students choose not to use some of these constants)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

const char NULL_CHAR     { 'z' };
const char UP_CHAR       { 'w' };
const char DOWN_CHAR     { 's' };
const char QUIT_CHAR     { 'q' };
const char CREATE_CHAR   { 'c' };
const char BLOCKING_CHAR { 'b' };
const char COMMAND_CHAR  { 'o' };

const string ANSI_START { "\033[" };
const string START_COLOUR_PREFIX {"1;"};
const string START_COLOUR_SUFFIX {"m"};
const string STOP_COLOUR  {"\033[0m"};

const unsigned int COLOUR_IGNORE    { 0 }; // this is a little dangerous but should work out OK
const unsigned int COLOUR_GREEN     { 32 };
const unsigned int COLOUR_BLUE      { 34 };
const unsigned int COLOUR_RED       { 31 };
const unsigned int COLOUR_WHITE     { 37 };

const int NUM_ROWS {40};
const int NUM_COLS {100};

#pragma clang diagnostic pop

// Types

// Using signed and not unsigned to avoid having to check for ( 0 - 1 ) being very large
struct position { int row; int col; };

struct fishie 
{
    position position {20,5};
    bool swimming = true;
    unsigned int colour = COLOUR_BLUE;
    float speed = 10.0;
};



bool hit = false;
int score = 0;


// Globals

struct termios initialTerm;
default_random_engine generator;
uniform_int_distribution<int> startOpening(10,30);
uniform_int_distribution<int> powerSpawn(10,30);

struct pipes
{
    position position {1, NUM_COLS};
    int height = startOpening(generator);
};

struct powerUp{
    position position {powerSpawn(generator), 0};
    bool attained = false;
    unsigned int colour = COLOUR_RED;
};

pipes currentPipe; 

typedef vector<pipes> pipevector;

// Utilty Functions

// These two functions are taken from StackExchange and are 
// all of the "magic" in this code.
auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type 
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    newTerm.c_cc[VMIN] = 1;
 
    // Set the terminal attributes for STDIN immediately
    auto result { tcsetattr(fileno(stdin), TCSANOW, &newTerm) };
    if ( result < 0 ) { cerr << "Error setting terminal attributes [" << result << "]" << endl; }
}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr( fileno( stdin ), TCSANOW, &initialTerm );
}
auto SetNonblockingReadState( bool desiredState = true ) -> void
{
    auto currentFlags { fcntl( 0, F_GETFL ) };
    if ( desiredState ) { fcntl( 0, F_SETFL, ( currentFlags | O_NONBLOCK ) ); }
    else { fcntl( 0, F_SETFL, ( currentFlags & ( ~O_NONBLOCK ) ) ); }
    cerr << "SetNonblockingReadState [" << desiredState << "]" << endl;
}
// Everything from here on is based on ANSI codes
// Note the use of "flush" after every write to ensure the screen updates
auto ClearScreen() -> void { cout << ANSI_START << "2J" << flush; }
auto MoveTo( unsigned int x, unsigned int y ) -> void { cout << ANSI_START << x << ";" << y << "H" << flush; }
auto HideCursor() -> void { cout << ANSI_START << "?25l" << flush; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" << flush; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999,999);
    cout << ANSI_START << "6n" << flush ;
    string responseString;
    char currentChar { static_cast<char>( getchar() ) };
    while ( currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0,2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString { responseString.substr( 0, semicolonLocation ) };
    auto colsString { responseString.substr( ( semicolonLocation + 1 ), responseString.size() ) };
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    //auto rows = stoul( rowsString );
    //auto cols = stoul( colsString );
    position returnSize { static_cast<int>(NUM_ROWS), static_cast<int>(NUM_COLS) };
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}
auto MakeColour( string inputString, 
                 const unsigned int foregroundColour = COLOUR_WHITE,
                 const unsigned int backgroundColour = COLOUR_IGNORE ) -> string
{
    string outputString;
    outputString += ANSI_START;
    outputString += START_COLOUR_PREFIX;
    outputString += to_string( foregroundColour );
    if ( backgroundColour ) 
    { 
        outputString += ";";
        outputString += to_string( ( backgroundColour + 10 ) ); // Tacky but works
    }
    outputString += START_COLOUR_SUFFIX;
    outputString += inputString;
    outputString += STOP_COLOUR;
    return outputString;
}

// Fish Logic

auto UpdateFishPositions( fishie & currentFish, char currentChar ) -> void
{
    if ( currentChar == UP_CHAR and currentFish.position.row != 0)    { 
        currentFish.position.row -= 1;
    }
    if ( currentChar == DOWN_CHAR and currentFish.position.row != NUM_ROWS - 5 )  { 
        currentFish.position.row += 1;
        }

}

auto CreateFishie( fishie & fish ) -> void
{
    //cerr << "Creating Fishie" << endl;
    fishie newFish { 
        .position = { .row = 1, .col = 1 } ,
        .swimming = true ,
        .colour = COLOUR_BLUE,
        .speed = 1.0
    };
    fish = newFish;
}

auto DrawFishies( fishie currentFish ) -> void
{
    MoveTo( currentFish.position.row, currentFish.position.col ); 
    cout << MakeColour( "><((('>", currentFish.colour ) << flush; 
}

// Creates a pipe and pushes it into array of pipes
auto CreatePipe( pipevector & pipeList ) -> void
{
    pipes newPipe { 
        .position = { .row = 1, .col = NUM_COLS } ,
        .height = startOpening(generator)
    };
    pipeList.push_back(newPipe);
}


// moves all pipes to the left and erases the pipe the fish passes through
auto UpdatePipe (pipevector & pipeList) -> void {
    for (int i = 0; i < static_cast<int>(pipeList.size()); i++){
        pipeList[i].position.col--;
            if (pipeList[i].position.col <= 0){
                pipeList.erase(pipeList.begin()); 
            }   
    }
    currentPipe = pipeList[0];
}

// ouputs the pipes 
auto DrawPipe( pipevector pipeList) -> void
{
    for (auto newPipe : pipeList){
        for(int i = 0; i < 35; i++){
            if (i > newPipe.height and i < newPipe.height + 5){
                MoveTo ( newPipe.position.row + i, newPipe.position.col);
                cout << flush;
            }
            else{
                MoveTo ( newPipe.position.row + i, newPipe.position.col);
                cout << MakeColour("****", COLOUR_GREEN) << flush;
            }
            
        }
    }
}

auto CreatePowerUp (powerUp & slowTime) -> void {
    powerUp newSlowTime{
        .position = {.row = powerSpawn(generator), .col = 100},
        .attained = false,
        .colour = COLOUR_RED
    };
    slowTime = newSlowTime;
}

auto DrawPowerUp (powerUp & slowTime) -> void{

    MoveTo( slowTime.position.row, slowTime.position.col); 
    cout << MakeColour( "O", slowTime.colour ) << flush; 

    slowTime.position.col --;

}

auto CheckCollisionsPipe (pipes pipe, fishie fish) -> bool{
    if(pipe.position.col >= 5 && pipe.position.col <= 12){
        if(fish.position.row <= pipe.height + 1 || fish.position.row > pipe.height + 5){
                return true;
            }
        }
    return false;
}

auto CheckCollisionsPowerUp (int col, int row, fishie fish) -> bool{
    if (col >= 5 && col <= 12){
        if(fish.position.row == row){
            return true;
        }
    }
    return false;
}


auto main() -> int
{
    // Set Up the system to receive input
    SetupScreenAndInput();

    // Check that the terminal size is large enough for our fishies
    const position TERMINAL_SIZE { GetTerminalSize() };
    if ( ( TERMINAL_SIZE.row < 30 ) or ( TERMINAL_SIZE.col < 50 ) )
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl <<  "Terminal window must be at least 30 by 50 to run this game" << endl;
        return EXIT_FAILURE;
    }

    // State Variables
    //fishvector fishies;
    fishie fish;
    powerUp slowTime;
    pipevector pipeList;
    unsigned int ticks {0};

    char currentChar { CREATE_CHAR }; // the first act will be to create a fish
    string currentCommand;

    bool allowBackgroundProcessing { true };
    bool showCommandline { false };

    auto startTimestamp { chrono::steady_clock::now() };
    auto endTimestamp { startTimestamp };
    int elapsedTimePerTick { 100 }; // Every 0.1s check on things
    int ticksIncrement = 10;

    //int tickIncrease = 1;
    //int widthDecreaser = 40;
    
    SetNonblockingReadState( allowBackgroundProcessing );
    ClearScreen();
    HideCursor();

    CreatePipe (pipeList);
   

    while( currentChar != QUIT_CHAR and !hit)
    {
        endTimestamp = chrono::steady_clock::now();
        auto elapsed { chrono::duration_cast<chrono::milliseconds>( endTimestamp - startTimestamp ).count() };
        // We want to process input and update the world when EITHER  
        // (a) there is background processing and enough time has elapsed
        // (b) when we are not allowing background processing.
        if ( 
                 ( allowBackgroundProcessing and ( elapsed >= elapsedTimePerTick ) )
              or ( not allowBackgroundProcessing ) 
           )
        {
            ticks++;
            cerr << "Ticks [" << ticks << "] allowBackgroundProcessing ["<< allowBackgroundProcessing << "] elapsed [" << elapsed << "] currentChar [" << currentChar << "] currentCommand [" << currentCommand << "]" << endl;
            if ( currentChar == BLOCKING_CHAR ) // Toggle background processing
            {
                allowBackgroundProcessing = not allowBackgroundProcessing;
                SetNonblockingReadState( allowBackgroundProcessing );
            }
            if ( currentChar == COMMAND_CHAR ) // Switch into command line mode
            {
                allowBackgroundProcessing = false;
                SetNonblockingReadState( allowBackgroundProcessing ); 
                showCommandline = true;
            }
            if ( currentCommand.compare( "resume" ) == 0 ) { cerr << "Turning off command line" << endl; showCommandline = false; }
            if ( ( currentChar == UP_CHAR ) or ( currentChar == DOWN_CHAR )){
                UpdateFishPositions (fish, currentChar);
            }
            ClearScreen();

            DrawFishies( fish );

            // outputting instructions
            MoveTo(37 , 70);
            cout << "Score: " << score << flush;
            MoveTo(38 , 70);
            cout << "Press 'w' to move up"  << flush;
            MoveTo(39 , 70);
            cout << "Press 's' to move down"  << flush;
            MoveTo(40 , 70);
            cout << "Press 'q' to quit"  << flush;

            // increment score when passes through
            if (ticks % 40 == 20 && ticks >= 100){score ++;}
            // creates pipes every 4 an interval
            if (ticks % 40 == 0){
                CreatePipe (pipeList);

                // increases speed of game
                if (elapsedTimePerTick > 40){
                    elapsedTimePerTick -= ticksIncrement;
                }
            }

            if (ticks % 120 == 20){
                CreatePowerUp(slowTime);
                slowTime.attained = false;
            } 
            UpdatePipe (pipeList);
            DrawPipe (pipeList);
            if (ticks >= 138 && not(slowTime.attained)){
                DrawPowerUp (slowTime);
            }
            if (CheckCollisionsPowerUp (slowTime.position.col, slowTime.position.row, fish )){
                slowTime.attained = true;
                elapsedTimePerTick = 100;
            }
            /*
            if (slowTime.position.col >= 5 && slowTime.position.col <= 12){
                if(fish.position.row == slowTime.position.row){
                    slowTime.attained = true;
                    elapsedTimePerTick = 100;
                }
            */
            //checking collisions
            //if the fish within the width of the first pipe
            //if the fish above the start of the opening OR below the bottom of the opening then set hit to true
/*
    if(pipeList[0].position.col >= 5 && pipeList[0].position.col <= 12){
        if(fish.position.row <= pipeList[0].height + 1 || fish.position.row > pipeList[0].height + 5){
                return true;
            }
        }
*/
            if(CheckCollisionsPipe(pipeList[0], fish)){
                hit = true;
            }


            if ( showCommandline )
            {
                cerr << "Showing Command Line" << endl;
                MoveTo( 21, 1 ); 
                ShowCursor();
                cout << "Command:" << flush;
            }
            else { HideCursor(); }

            // Clear inputs in preparation for the next iteration
            startTimestamp = endTimestamp;    
            currentChar = NULL_CHAR;
            currentCommand.clear();
        }
        // Depending on the blocking mode, either read in one character or a string (character by character)
        if ( showCommandline )
        {
            while ( read( 0, &currentChar, 1 ) == 1 && ( currentChar != '\n' ) )
            {
                cout << currentChar << flush; // the flush is important since we are in non-echoing mode
                currentCommand += currentChar;
            }
            cerr << "Received command [" << currentCommand << "]" << endl;
            currentChar = NULL_CHAR;
        }
        else
        {
            read( 0, &currentChar, 1 );
        }
        
    }
    // Tidy Up and Close Down
    ShowCursor();
    SetNonblockingReadState( false );
    TeardownScreenAndInput();
    cout << endl; // be nice to the next command
    return EXIT_SUCCESS;
}

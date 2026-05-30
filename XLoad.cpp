// C++
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <cassert>

// FTDI
#ifdef _WIN32
#include "FTDI\ftd2xx.h"    // includes 'windows.h' (namespaced)
#else
#include "serial_posix.h"
#include <limits.h>

#ifndef UINT_MAX
#define UINT_MAX 0xffffffff
#endif

#define STD_OUTPUT_HANDLE 0
#define VK_ESCAPE 27

inline void Sleep(int ms) {
    usleep(ms * 1000);
}

inline short GetAsyncKeyState(int vKey) {
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

    if(ch == 27 && vKey == VK_ESCAPE) {
        return (short)0x8000;
    }
    return 0;
}

inline void *GetStdHandle(int) { return nullptr; }
inline void SetConsoleTextAttribute(void *, int color) {
    if (color == 10) printf("\033[32m");
    else if (color == 12) printf("\033[31m");
    else if (color == 15) printf("\033[0m");
}
#endif


// Defines
#define BAUDRATE_IMG        500000          // Baudrate when using the image loader
#define BAUDRATE            12000000        // Baudrate for terminal
#define PRG_BUFFER          512             // Buffer size
#define NUM_PROGRAMS        128             // EEPROM programs
#define OK_CODE             0x00            // RET code (42 for XFM1, 00 for XFM2/XVA1)
#define LATENCY_STD         16              // milliseconds, latency timer for the FTDI
#define LATENCY_RT          0               // realtime (high driver CPU usage)

// Types
using namespace std;
using uchar = unsigned char;
using uint = unsigned int;

enum class FLASH_TYPE {
    IMAGE = 0,
    TUNING = 1,
    WAVETABLE
};


// Forward decl
//

void Terminal();
void Color( int color );
void PrintError( const char error[] );
void CloseDevice();
int ProcessLine( string s );
int OpenDevice( uint baudrate, uint latency );
int SetFlashDump( string filename, uint baudrate, FLASH_TYPE flash_type );


// Error messages

// main
const char ERROR_PORT[] = "Error opening port.";

// terminal
const char ERROR_CONNECTING_DEVICE[] = "   Couldn't connect to device.\n";
const char ERROR_SETTING_DEVICE[] = "   Error setting device parameters.\n";
const char ERROR_INITIALIZING[] = "   Error initializing program.\n";
const char ERROR_INVALID_PROGRAM_NUMBER[] = "   Invalid program number (0-127).\n";
const char ERROR_INVALID_PARAM_NUMBER[] = "   Invalid parameter number (0-511).\n";
const char ERROR_INVALID_PARAM_VALUE[] = "   Invalid parameter value (0-255).\n";
const char ERROR_READING_PROGRAM[] = "   Error reading program.\n";
const char ERROR_WRITING_PROGRAM[] = "   Error writing program.\n";
const char ERROR_WRITING_CHANNEL[] = "   Error writing channel.\n";
const char ERROR_OPENING_FILE[] = "   Error opening file.\n";
const char ERROR_INVALID_CHANNEL[] = "   Invalid channel (0 = omni, 1-16).\n";
const char ERROR_LOADING_PROGRAM[] = "   Error loading program.\n";

// Globals

FT_HANDLE ft_port;


//---------------------------------------------------------------------------------------------------------------------
// main()
//
// Usage: XLoad [option] [filename]
//
// Options: 
//          -img:   Load flash image file
//---------------------------------------------------------------------------------------------------------------------
int main( int argc, char** argv ) {
    int err = 0;
    if( argc < 2 ) {
        if( argc == 1 ) {
            err = OpenDevice( BAUDRATE, LATENCY_STD );
            if( err ) {
                PrintError( ERROR_PORT );
                return err;
            }

            Terminal();
        }
        else {
            err = -1;
        }
    }
    else {
        string arg1 = argv[ 1 ];

        if( arg1 == "-img" ) {
            Color( 10 );
            cout << endl << "Loading Image file:" << endl;
            err = SetFlashDump( argv[ 2 ], BAUDRATE_IMG, FLASH_TYPE::IMAGE );

            if( err == 0 ) {
                cout << endl << "done." << endl;
            }
        }
        else if( arg1 == "-h" ) {
            Color( 10 );
            cout <<
                "\nUsage:\tXLoad [ option ]\n\nOptions:\n"
                "\t-img filename\t\tLoad Synthesizer Image\n"
                "\t\"<command>\"\t\tRun command\n\n\n"
                "Examples:\n\tXLoad \"get_bank ~/backup.bank\"\n"
                "\tXLoad \"* 5\"\n"
                "\tXLoad \". ~/recording.wav\"\n";
        }
        else {
            err = OpenDevice( BAUDRATE, LATENCY_STD );
            if( err ) {
                PrintError( ERROR_PORT );
                return err;
            }

            Color( 10 );
            cout << endl;
            ProcessLine( argv[ 1 ] );

            CloseDevice();
        }
    }


    if( err ) {
        if( -1 == err ) {
        }
        else {
            Color( 12 );
            cout << "\n\nerror %i.\n" << err;
        }
    }

    Color( 15 );
    return 0;
}

void Color( int color ) {
    ::SetConsoleTextAttribute( ::GetStdHandle( STD_OUTPUT_HANDLE ), color );
}

void PrintError( const char error[] ) {
    Color( 12 );
    cout << endl << error << endl << endl;
    Color( 15 );
}

//---------------------------------------------------------------------------------------------------------------------
// Terminal code
//
//---------------------------------------------------------------------------------------------------------------------


//---------------------------------------------------------------------------------------------------------------------
// helpers
//
//---------------------------------------------------------------------------------------------------------------------
vector<string> split( string& text, string brakes ) {
    vector<string> out;

    size_t last = 0;
    for( ;; ) {
        size_t pos = text.find_first_of( brakes, last );
        if( pos != string::npos ) {
            auto sub = text.substr( last, pos - last );
            if( !sub.empty() ) {
                out.push_back( sub );
            }

            last = pos + 1;
        }
        else {
            auto sub = text.substr( last, text.length() - last );
            if( !sub.empty() ) {
                out.push_back( sub );
            }
            break;
        }
    }

    return out;
}

//---------------------------------------------------------------------------------------------------------------------
// PrintProgress() - Scrolling text progress bar with spinner
//
// Prints one line per call (uses endl to flush immediately).
// Throttle calls from the loop so the output stays readable.
//---------------------------------------------------------------------------------------------------------------------
// Print one progress line on its own row.
// Call this at milestones only (e.g. every 8 steps) so the output stays tidy.
void PrintProgress( int current, int total, const char* label ) {
    static const char spinner[] = { '|', '/', '-', '\\' };
    const int bar_width = 36;
    int filled = total > 0 ? (current * bar_width) / total : 0;
    int pct    = total > 0 ? (current * 100)      / total : 0;

    cout << "  " << spinner[ current % 4 ] << ' '
         << left << setw(12) << label
         << " [";
    for( int i = 0; i < bar_width; ++i ) {
        if      ( i < filled )                     cout << '=';
        else if ( i == filled && current < total ) cout << '>';
        else                                       cout << ' ';
    }
    cout << "] "
         << right << setw(3) << current << "/" << total
         << " (" << setw(3) << pct << "%)" << endl;  // endl flushes immediately
}

bool is_numeric( string str ) {
    int iteration = 0;
    bool result = true;

    while( iteration < (int) str.length() )
    {
        if( !isdigit( str[ iteration ] ) )
        {
            result = false;
            break;
        }

        iteration++;
    }

    return result;
}

//---------------------------------------------------------------------------------------------------------------------
// OpenDevice(), CloseDevice()
//
//---------------------------------------------------------------------------------------------------------------------
int OpenDevice( uint baud_rate, uint latency ) {
    FT_STATUS st;

    // Debug only
    //

    if( 0 ) {
        DWORD numDevs;
        st = FT_CreateDeviceInfoList( &numDevs );

        if( numDevs > 0 ) {
            FT_DEVICE_LIST_INFO_NODE* devInfo;

            // allocate storage for list based on numDevs
            devInfo = (FT_DEVICE_LIST_INFO_NODE*) malloc( sizeof( FT_DEVICE_LIST_INFO_NODE ) * numDevs );

            // get the device information list

            st = FT_GetDeviceInfoList( devInfo, &numDevs );
            if( st == FT_OK ) {
                for( uint i = 0; i < numDevs; i++ ) {
                    printf( "Dev %d:\n", i );
                    printf( " Flags=0x%x\n", (uint)devInfo[ i ].Flags );
                    printf( " Type=0x%x\n", (uint)devInfo[ i ].Type );
                    printf( " ID=0x%x\n", (uint)devInfo[ i ].ID );
                    printf( " LocId=0x%x\n", devInfo[ i ].LocId );
                    printf( " SerialNumber=%s\n", devInfo[ i ].SerialNumber );
                    printf( " Description=%s\n", devInfo[ i ].Description );
                    //printf( " ftHandle=0x%x\n", devInfo[ i ].ftHandle );
                }
            }
        }
    }


    // Open port
    //

    st = FT_OpenEx( (PVOID) "Digilent Adept USB Device B", FT_OPEN_BY_DESCRIPTION, &ft_port );

    if( st != FT_OK ) {
        cout << ERROR_CONNECTING_DEVICE;
        return 1;
    }


    // Set port parameters
    //

    st = FT_SetBaudRate( ft_port, baud_rate );
    st = FT_SetDataCharacteristics( ft_port, FT_BITS_8, FT_STOP_BITS_1, FT_PARITY_NONE );
    st = FT_SetTimeouts( ft_port, UINT_MAX, UINT_MAX );
    st = FT_SetLatencyTimer( ft_port, latency );
    st = FT_SetUSBParameters( ft_port, 4096, 0 );

    if( st != FT_OK ) {
        cout << ERROR_SETTING_DEVICE;
        return 1;
    }

    return 0;
}

void CloseDevice() {
    FT_SetTimeouts( ft_port, 100, 100 );
    FT_Close( ft_port );
}

//---------------------------------------------------------------------------------------------------------------------
// SetFlashDump()
//
//---------------------------------------------------------------------------------------------------------------------
int SetFlashDump( string filename, uint baudrate, FLASH_TYPE flash_type ) {

    // Images are loaded using the CMOD_A7_Loader.bit file, which has the COM set at 500kbps
    //

    if( OpenDevice( baudrate, LATENCY_RT ) != 0 ) {
        return 1;
    }

    // Open file
    ifstream infile;
    infile.open( filename, ios::in | ios::binary );
    if( !infile.is_open() ) {
        cout << ERROR_OPENING_FILE;
        return 1;
    }


    // Prepare file of 0x220000 bytes
    uint FILE_SIZE;
    uint pages;
    uchar cmd_erase, cmd_write;

    switch( flash_type ) {
        case FLASH_TYPE::IMAGE:
            FILE_SIZE = 34 * 256 * 256;
            pages = 34 * 256;
            cmd_erase = '{';
            cmd_write = '}';
            break;

        case FLASH_TYPE::TUNING:
            FILE_SIZE = 4 * 256 * 128;;
            pages = 2 * 256;
            cmd_erase = '#';
            cmd_write = 't';
            break;

        case FLASH_TYPE::WAVETABLE:
            FILE_SIZE = 3 * 256 * 1024;
            pages = 12 * 256;
            cmd_erase = 'V';
            cmd_write = 'v';
            break;

        default:
            return 2;
    }

    vector<char> buffer( FILE_SIZE );
    memset( &buffer[ 0 ], 0, FILE_SIZE );

    // Read data
    infile.read( &buffer[ 0 ], FILE_SIZE );


    DWORD len;
    FT_STATUS st;

    // Send full_flash_erase command
    cout << "Erasing flash..." << endl;

    st = FT_Write( ft_port, &cmd_erase, 1, &len );
    if( st != FT_OK ) {
        return 3;
    }

    // Check return code
    uchar code;
    st = FT_Read( ft_port, &code, 1, &len );
    if( st != FT_OK || code != 0 ) {
        return 4;
    }


    // Send full_flash_write command
    st = FT_Write( ft_port, &cmd_write, 1, &len );
    if( st != FT_OK ) {
        return 5;
    }

    // Iterate thru all pages of 256 bytes each
    for( uint i = 0; i < pages; ++i ) {

        if( flash_type == FLASH_TYPE::IMAGE ) {
            // Write first byte (takes longer time to write, as it prepares the page)
            st = FT_Write( ft_port, &buffer[ i * 256 ], 1, &len );
            if( st != FT_OK || len != 1 ) {
                return 6;
            }

            // This should be ACK, but the loader is already out so hack it.
            ::Sleep( 10 );

            // Write 255 more bytes
            st = FT_Write( ft_port, &buffer[ i * 256 + 1 ], 255, &len );
            if( st != FT_OK || len != 255 ) {
                return 7;
            }

            // Check return code
            st = FT_Read( ft_port, &code, 1, &len );
            if( st != FT_OK || len != 1 ) {
                return 8;
            }
        }
        else {
            for( uint j = 0; j < 256; ++j ) {
                st = FT_Write( ft_port, &buffer[ i * 256 + j ], 1, &len );
                if( st != FT_OK || len != 1 ) {
                    return 9;
                }

                st = FT_Read( ft_port, &code, 1, &len );
                if( st != FT_OK || len != 1 || code != 0 ) {
                    return 10;
                }
            }
        }

        if( code != 0 )
            cout << "X";

        if( (i + 1) % (pages / 16 + 1) == 0 || i + 1 == pages )
            PrintProgress( i + 1, pages, "Writing" );
    }
    cout << endl;

    // Check return code
    st = FT_Read( ft_port, &code, 1, &len );
    if( st != FT_OK || code != 0 ) {
        cout << "Error writing image.";
        return 11;
    }

    infile.close();

    CloseDevice();
    return 0;
}

//---------------------------------------------------------------------------------------------------------------------
// InitializeBank()
//
//---------------------------------------------------------------------------------------------------------------------
int InitializeBank() {
    FT_STATUS st;
    DWORD len;
    uchar buffer[ PRG_BUFFER ];

    // Send 'bank erase' command to device
    uchar cmd = '$';
    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK )
        return 1;

    // Twiddle thumbs
    cout << "  Erasing bank..." << endl;
    for( int i = 0; i < 128; ++i ) {
        ::Sleep( 100 );
        if( (i + 1) % 8 == 0 || i + 1 == 128 )
            PrintProgress( i + 1, 128, "Erasing" );
    }
    cout << endl;

    // Wait for status response
    st = FT_Read( ft_port, &buffer, 1, &len );
    if( st != FT_OK )
        return 2;

    if( buffer[ 0 ] != OK_CODE ) {
        return 3;
    }

    return 0;
}

//---------------------------------------------------------------------------------------------------------------------
// GetBank()
//
//---------------------------------------------------------------------------------------------------------------------
int GetBank( string filename ) {

    ofstream outfile;
    outfile.open( filename, ios::out | ios::binary );

    if( !outfile.is_open() ) {
        cout << ERROR_OPENING_FILE;
        return 1;
    }

    // Prepare buffer
    uchar buffer[ PRG_BUFFER ];

    DWORD len;
    FT_STATUS st;


    // Send 'Reset EEPROM byte counter'
    uchar cmd = '>';

    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK )
        return 1;


    // Loop thru all programs
    cout << "  Reading bank..." << endl;
    for( int j = 0; j < NUM_PROGRAMS; ++j ) {
        // Get two pages
        st = FT_Read( ft_port, buffer, 512, &len );
        if( st != FT_OK )
            return 3;

        // Write to file
        outfile.write( (const char*) buffer, PRG_BUFFER );

        if( (j + 1) % 8 == 0 || j + 1 == NUM_PROGRAMS )
            PrintProgress( j + 1, NUM_PROGRAMS, "Reading" );
    }
    cout << endl;

    outfile.close();

    return 0;
}

//---------------------------------------------------------------------------------------------------------------------
// PutBank()
//
//---------------------------------------------------------------------------------------------------------------------
int PutBank( string filename ) {

    ifstream infile;
    infile.open( filename, ios::in | ios::binary );
    if( !infile.is_open() ) {
        cout << ERROR_OPENING_FILE;
        return 1;
    }

    const uint PAGE_SIZE = 128;
    char buffer[ PAGE_SIZE ];
    DWORD len;
    FT_STATUS st;


    // Iterate thru all programs
    cout << "  Writing bank..." << endl;
    for( int j = 0; j < NUM_PROGRAMS; ++j ) {
        uchar data[ 2 ];

        // Send 'WriteProgramToEEPROM' msg and program number
        data[ 0 ] = '}';
        data[ 1 ] = uchar( j );

        st = FT_Write( ft_port, data, 2, &len );
        if( st != FT_OK ) {
            cout << "\n   Write error sending program header (" << j << ").\n";
            return 2;
        }

        // Check return code
        uchar code;
        st = FT_Read( ft_port, &code, 1, &len );
        if( st != FT_OK ) {
            cout << "\n   Read error waiting for ack (" << j << ").\n";
            return 3;
        }

        if( code != data[ 1 ] ) {
            cout << "\n   Bad ack for program " << j
                 << " (got 0x" << hex << int(code) << dec << ").\n";
            return 4;
        }

        // Iterate thru all four chunks for each program (512 bytes = 128x4)
        for( uint i = 0; i < 4; ++i ) {
            infile.read( buffer, PAGE_SIZE );

            st = FT_Write( ft_port, buffer, PAGE_SIZE, &len );
            if( st != FT_OK ) {
                cout << "\n   Write error on program " << j << " chunk " << i << ".\n";
                return 5;
            }

            // Check return code
            uchar code;
            st = FT_Read( ft_port, &code, 1, &len );
            if( st != FT_OK ) {
                cout << "\n   Read error on program " << j << " chunk " << i << ".\n";
                return 6;
            }

            if( code != 0x80 ) {
                cout << "\n   Unexpected status 0x" << hex << int(code) << dec
                     << " on program " << j << " chunk " << i << ".\n";
                return 7;
            }

            ::Sleep( 20 );
        }

        if( (j + 1) % 8 == 0 || j + 1 == NUM_PROGRAMS )
            PrintProgress( j + 1, NUM_PROGRAMS, "Writing" );
    }
    cout << endl;

    infile.close();

    return 0;
}

//---------------------------------------------------------------------------------------------------------------------
// LoadProgram
//
//---------------------------------------------------------------------------------------------------------------------
void LoadProgram( string str ) {

    auto elem = split( str, " " );
    if( elem.size() < 2 )
        return;

    char buffer[ PRG_BUFFER ];
    DWORD len;
    FT_STATUS st;

    ifstream infile;
    infile.open( elem[ 1 ], ios::out | ios::binary );

    if( infile.is_open() ) {
        infile.read( buffer, PRG_BUFFER );
        infile.close();

        // Send inject command
        uchar cmd = 'j';
        st = FT_Write( ft_port, &cmd, 1, &len );
        if( st != FT_OK ) {
            cout << ERROR_LOADING_PROGRAM;
            return;
        }


        // Send data
        st = FT_Write( ft_port, buffer, PRG_BUFFER, &len );
        if( st != FT_OK ) {
            cout << ERROR_LOADING_PROGRAM;
            return;
        }

        // Check return code
        uchar code;
        st = FT_Read( ft_port, &code, 1, &len );
        if( st != FT_OK ) {
            cout << ERROR_LOADING_PROGRAM;
            return;
        }

        if( buffer[ 0 ] != OK_CODE ) {
            cout << ERROR_LOADING_PROGRAM;
            return;
        }
    }
    else {
        cout << ERROR_OPENING_FILE;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// Initialize program
//
//---------------------------------------------------------------------------------------------------------------------
void Initialize( string str ) {
    auto elem = split( str, " " );

    DWORD len;
    FT_STATUS st;

    if( elem.size() == 1 ) {

        // Send initialize command  
        uchar cmd = 'i';
        st = FT_Write( ft_port, &cmd, 1, &len );
        if( st != FT_OK ) {
            cout << ERROR_INITIALIZING;
            return;
        }

        // Check return code
        uchar code;
        st = FT_Read( ft_port, &code, 1, &len );
        if( st != FT_OK ) {
            cout << ERROR_INITIALIZING;
            return;
        }

        if( code != 0 ) {
            cout << ERROR_INITIALIZING;
            return;
        }
    }
    else {
        LoadProgram( str );
    }
}

//---------------------------------------------------------------------------------------------------------------------
// GetProgramDump
//
//---------------------------------------------------------------------------------------------------------------------
void GetProgramDump( string str ) {

    auto elem = split( str, " " );

    DWORD len;
    FT_STATUS st;

    // Send dump command
    uchar cmd = 'd';
    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK ) {
        return;
    }


    // Get data
    uchar buffer[ PRG_BUFFER ];
    st = FT_Read( ft_port, buffer, PRG_BUFFER, &len );
    if( st != FT_OK ) {
        return;
    }


    if( elem.size() == 1 ) {
        // To display
        //

        stringstream s;
        s << "  ";
        for( uint i = 0; i < PRG_BUFFER; ++i ) {

            s << uppercase << setw( 2 ) << setfill( '0' ) << hex << int( buffer[ i ] ) << " ";
            if( ( i % 25 ) == 24 )
                s << endl << "  ";
        }

        s << endl;
        cout << s.str();
    }
    else {
        // To file
        //

        ofstream outfile;
        outfile.open( elem[ 1 ], ios::out | ios::binary );
        if( outfile.is_open() ) {
            for( uint i = 0; i < PRG_BUFFER; ++i )
                outfile << buffer[ i ];

            outfile.close();
        }
        else {
            cout << ERROR_OPENING_FILE;
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
// GetParam
//
//---------------------------------------------------------------------------------------------------------------------
void GetParam( string str ) {

    auto elem = split( str, " " );

    if( elem.size() < 2 )
        return;

    if( !is_numeric( elem[ 1 ] ) )
        return;

    int prm = atol( elem[ 1 ].c_str() );
    if( prm >= PRG_BUFFER ) {
        cout << ERROR_INVALID_PARAM_NUMBER;
        return;
    }

    uchar data[ 3 ];
    data[ 0 ] = 'g';
    data[ 1 ] = prm > 255 ? 255 : uchar( prm );
    data[ 2 ] = prm > 255 ? uchar( prm - 256 ) : 0;

    DWORD len;
    FT_STATUS st;

    st = FT_Write( ft_port, data, 2, &len );
    if( st != FT_OK ) {
        return;
    }

    if( prm > 255 ) {
        st = FT_Write( ft_port, &data[ 2 ], 1, &len );
        if( st != FT_OK ) {
            return;
        }
    }

    // Check return code
    uchar code;
    st = FT_Read( ft_port, &code, 1, &len );
    if( st != FT_OK ) {
        return;
    }

    cout << "  " << int( code ) << endl;
}

//---------------------------------------------------------------------------------------------------------------------
// SetParam
//
//---------------------------------------------------------------------------------------------------------------------
void SetParam( string str ) {
    using namespace std;

    auto elem = split( str, " " );

    if( elem.size() < 3 )
        return;

    if( !is_numeric( elem[ 1 ] ) || !is_numeric( elem[ 2 ] ) )
        return;

    int prm = atol( elem[ 1 ].c_str() );
    if( prm >= PRG_BUFFER ) {
        cout << ERROR_INVALID_PARAM_NUMBER;
        return;
    }


    int value = atol( elem[ 2 ].c_str() );
    if( value > 255 ) {
        cout << ERROR_INVALID_PARAM_VALUE;
        return;
    }

    uchar data[ 4 ];
    data[ 0 ] = 's';
    data[ 1 ] = prm > 255 ? 255 : uchar( prm );
    data[ 2 ] = prm > 255 ? uchar( prm - 256 ) : 0;
    data[ 3 ] = uchar( value );

    DWORD len;
    FT_STATUS st;

    // Send setparam command, and the parameter number  
    st = FT_Write( ft_port, data, 2, &len );
    if( st != FT_OK ) {
        return;
    }

    // Handle parameter numbers > 255
    if( prm > 255 ) {
        st = FT_Write( ft_port, &data[ 2 ], 1, &len );
        if( st != FT_OK ) {
            return;
        }
    }

    // Send parameter value
    st = FT_Write( ft_port, &data[ 3 ], 1, &len );
    if( st != FT_OK ) {
        return;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// ReadProgram
//
//---------------------------------------------------------------------------------------------------------------------
void ReadProgram( string str ) {

    auto elem = split( str, " " );

    if( elem.size() < 2 )
        return;

    if( !is_numeric( elem[ 1 ] ) )
        return;

    int prm = atol( elem[ 1 ].c_str() );
    if( prm >= NUM_PROGRAMS ) {
        cout << ERROR_INVALID_PROGRAM_NUMBER;
        return;
    }

    uchar data[ 2 ];
    data[ 0 ] = 'r';
    data[ 1 ] = uchar( prm );

    DWORD len;
    FT_STATUS st;

    // Send read_program command, and the program number
    st = FT_Write( ft_port, data, 2, &len );
    if( st != FT_OK ) {
        return;
    }

    // Check return code
    uchar code;
    st = FT_Read( ft_port, &code, 1, &len );
    if( st != FT_OK || code != 0 ) {
        cout << ERROR_READING_PROGRAM;
        return;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// WriteProgram
//
//---------------------------------------------------------------------------------------------------------------------
void WriteProgram( string str ) {
    auto elem = split( str, " " );

    if( elem.size() < 2 )
        return;

    if( !is_numeric( elem[ 1 ] ) )
        return;

    int prm = atol( elem[ 1 ].c_str() );
    if( prm >= NUM_PROGRAMS ) {
        cout << ERROR_INVALID_PROGRAM_NUMBER;
        return;
    }

    uchar data[ 2 ];
    data[ 0 ] = 'w';
    data[ 1 ] = uchar( prm );

    DWORD len;
    FT_STATUS st;

    // Send write_program command, and the program number
    st = FT_Write( ft_port, data, 2, &len );
    if( st != FT_OK ) {
        return;
    }

    // Check return code
    uchar code;
    st = FT_Read( ft_port, &code, 1, &len );
    if( st != FT_OK || code != 0 ) {
        cout << ERROR_WRITING_PROGRAM;
        return;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// SetChannel
//
//---------------------------------------------------------------------------------------------------------------------
void SetChannel( string str ) {

    DWORD len;
    FT_STATUS st;

    auto elem = split( str, " " );

    if( elem.size() < 2 ) {
        // Get MIDI Channel

        uchar cmd = '!';
        st = FT_Write( ft_port, &cmd, 1, &len );
        if( st != FT_OK ) {
            return;
        }

        // Check return code
        uchar code;
        st = FT_Read( ft_port, &code, 1, &len );
        if( st != FT_OK ) {
            return;
        }

        cout << int( code ) << endl;
    }
    else {
        // Set MIDI Channel
        for( uint i = 1; i < elem.size(); ++i ) {
            auto ch = elem[ i ];

            if( !is_numeric( ch ) )
                return;

            int prm = atol( ch.c_str() );
            if( prm > 16 ) {
                cout << ERROR_INVALID_CHANNEL;
                return;
            }

            uchar data[ 3 ];
            data[ 0 ] = '*';
            data[ 1 ] = 9 + i;
            data[ 2 ] = uchar( prm );



            // Send set_midi_channel command ( * N ), and the channel number
            st = FT_Write( ft_port, data, 3, &len );
            if( st != FT_OK ) {
                return;
            }

            // Check return code
            uchar code;
            st = FT_Read( ft_port, &code, 1, &len );
            if( st != FT_OK || code != data[ 2 ] ) {
                cout << ERROR_WRITING_CHANNEL;
                return;
            }
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
// NameProgram
//
//---------------------------------------------------------------------------------------------------------------------
void NameProgram( string str ) {

    const uint PROGRAM_NAME_OFFSET = 480;
    const uint PROGRAM_NAME_LEN = 24;


    auto elem = split( str, " " );

    DWORD len;
    FT_STATUS st;

    if( elem.size() == 1 ) {
        // Display name

        // Send dump command
        uchar cmd = 'd';

        st = FT_Write( ft_port, &cmd, 1, &len );
        if( st != FT_OK ) {
            return;
        }


        // Get data
        char buffer[ PRG_BUFFER ];

        st = FT_Read( ft_port, buffer, PRG_BUFFER, &len );
        if( st != FT_OK ) {
            return;
        }


        // Display program name
        string s( &buffer[ PROGRAM_NAME_OFFSET ], PROGRAM_NAME_LEN );
        cout << s << endl;
    }
    else {
        // Set name
        string name = str.substr( 2, str.length() - 2 );
        auto len = min( (uint)name.length(), PROGRAM_NAME_LEN );

        for( uint i = 0; i < PROGRAM_NAME_LEN; ++i ) {
            stringstream arg;
            arg << "s " << PROGRAM_NAME_OFFSET + i << " ";

            if( i < len ) {
                arg << (int) name[ i ];
            }
            else {
                arg << 32;
            }

            SetParam( arg.str() );
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
// GetAudioChunk
//
//---------------------------------------------------------------------------------------------------------------------
void GetAudioChunk( string str ) {

    const int AUDIO_BUFFER = 6 * 64;

    auto elem = split( str, " " );
    if( elem.size() < 2 )
        return;

    // Flush
    CloseDevice();
    OpenDevice( BAUDRATE, LATENCY_RT );

    DWORD len;
    FT_STATUS st;
    uchar buffer[ AUDIO_BUFFER ];

    union {
        uint ui;
        uchar b[ 4 ];
    } length;


    ofstream outfile;
    outfile.open( elem[ 1 ], ios::out | ios::binary );

    if( !outfile.is_open() ) {
        cout << ERROR_OPENING_FILE;
        return;
    }

    outfile << "RIFF";
    outfile << "    ";      // length placeholder
    outfile << "WAVE";
    outfile << "fmt ";

    length.ui = 18;
    outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

    outfile << uchar( 1 ) << uchar( 0 );
    outfile << uchar( 2 ) << uchar( 0 );

    length.ui = 96000;
    outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

    length.ui = 96000 * 2 * 3;
    outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

    outfile << uchar( 6 ) << uchar( 0 );

    length.ui = 24;
    outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

    outfile << "data";
    outfile << "    ";      // length placeholder

    Color( 12 );
    cout << "  Recording... ";
    Color( 10 );
    cout << "(hit ESC to finish).\n";


    // Send 'initialize streaming' and 'start streaming' commands

    uchar cmd = 'e';
    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK ) {
        return;
    }

    cmd = 'h';
    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK ) {
        return;
    }


    uint bytes = 0;

    // Main loop
    //

    for( ;; ) {
        // Samples
        st = FT_Read( ft_port, buffer, AUDIO_BUFFER, &len );
        if( st != FT_OK ) {
            return;
        }

        assert( len == AUDIO_BUFFER );

        for( uint i = 0; i < AUDIO_BUFFER; ++i ) {
            outfile << buffer[ i ];
            bytes++;
        }

        if( ::GetAsyncKeyState( VK_ESCAPE ) & 0x80000000 ) {
            break;
        }
    }


    // Finalize file
    if( outfile.is_open() ) {
        // Write 'data' chunk length
        outfile.seekp( 42 );
        length.ui = bytes;
        outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

        // Write 'RIFF' chunk length
        outfile.seekp( 4 );
        length.ui += 38;
        outfile << length.b[ 0 ] << length.b[ 1 ] << length.b[ 2 ] << length.b[ 3 ];

        outfile.close();
    }

    // Send terminate_streaming command
    cmd = 'f';
    st = FT_Write( ft_port, &cmd, 1, &len );
    if( st != FT_OK ) {
        return;
    }

    // Flush    
    CloseDevice();
    OpenDevice( BAUDRATE, LATENCY_STD );

    cout << "  done." << endl;
}

//---------------------------------------------------------------------------------------------------------------------
// Terminal
//
//---------------------------------------------------------------------------------------------------------------------

// Understands:
//
// g <param #>
// s <param #> <param value>
// r <prg>
// w <prg>
// * <channel1> <channel2>
// d
// d <filename>
// i
// i <filename>
// h 
// q

int ProcessLine( string s ) {

    auto elem = split( s, " " );
    string e = elem[ 0 ].c_str();

    // Initialize serves both as
    // a) init from default values, and
    // b) init from external file values (aka 'load program file').
    if( e == "i" )
        Initialize( s );

    // GetProgramDump serves both as
    // a) Get active program values, and
    // b) Save active program values to file (aka 'save program file).
    else if( e == "d" )
        GetProgramDump( s );

    // GetParam and SetParam
    else if( e == "g" )
        GetParam( s );
    else if( e == "s" )
        SetParam( s );

    // Read from and Write to the EEPROM
    else if( e == "r" )
        ReadProgram( s );
    else if( e == "w" )
        WriteProgram( s );

    // Get and Set program name
    else if( e == "n" )
        NameProgram( s );

    // Get and Set MIDI channel
    else if( e == "*" )
        SetChannel( s );

    // Load tuning definition file
    else if( e == "t" ) {
        if( elem.size() > 1 ) {
            CloseDevice();
            SetFlashDump( elem[ 1 ], BAUDRATE, FLASH_TYPE::TUNING );
            OpenDevice( BAUDRATE, LATENCY_STD );
        }
    }

    // Load wavetable (unused in XVA1, untested)
    else if( e == "wave" ) {
        if( elem.size() > 1 ) {
            CloseDevice();
            SetFlashDump( elem[ 1 ], BAUDRATE, FLASH_TYPE::WAVETABLE );
            OpenDevice( BAUDRATE, LATENCY_STD );
        }
    }

    // GetBank and PutBank load a whole EEPROM memory file
    else if( e == "get_bank" ) {
        if( elem.size() == 2 )
            GetBank( elem[ 1 ].c_str() );
    }
    else if( e == "put_bank" ) {
        if( elem.size() == 2 ) {
            PutBank( elem[ 1 ].c_str() );
        }
    }

    // InitializeBank, inits all EEPROM programs
    else if( e == "init_bank" ) {
        InitializeBank();
        ReadProgram( "r 0" );
    }

    // USB audio recording (experimental)
    else if( e == "." ) {
        GetAudioChunk( s );
    }

    // Help
    else if( e == "h" ) {
        cout << "\n  Commands:\n\n";
        cout << "  i\t\t\tInitializes program.\n";
        cout << "  i filename\t\tInitializes program from file (load).\n";
        cout << "  d\t\t\tShows all parameter values for current program.\n";
        cout << "  d filename\t\tWrites current program to filename (save).\n";
        cout << "  g N\t\t\tGets parameter N value.\n";
        cout << "  s N V\t\t\tSets parameter N to value V.\n";
        cout << "  r N\t\t\tReads program N.\n";
        cout << "  w N\t\t\tWrites current program to memory slot N.\n";
        cout << "  n\t\t\tGets current program name.\n";
        cout << "  n new name\t\tSets current program name.\n";
        cout << "  *\t\t\tDisplays current MIDI channel.\n";
        cout << "  * N\t\t\tSets MIDI channel to N (0 = omni).\n";
        cout << "  . filename\t\tStarts audio recording.\n";
        cout << "  t filename\t\tWrites a tuning definition file into device.\n";
        cout << "  wave filename\t\tWrites a wavetable file into device.\n";
        cout << "  get_bank filename\tReads a program bank from device.\n";
        cout << "  put_bank filename\tWrites a program bank file into device.\n";
        cout << "  init_bank\t\tInitializes all EEPROM programs (WARNING: all programs will be lost).\n";
        cout << "  h\t\t\tDisplays this help.\n";
        cout << "  q\t\t\tQuits.\n\n";
    }

    else if( e == "q" )
        return 1;

    return 0;
}

void Terminal() {
    Color( 10 );
    cout << "\nXLoad v2.01 ('q' to exit.)\n\n";

    for( ;; ) {
        cout << "# ";

        string s;
        getline( cin, s );

        if( s.empty() )
            continue;

        if( ProcessLine( s ) )
            return;
    }
}
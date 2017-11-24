/*
Commodore 64/128 - WiFiModem ESP8266
Copyright 2015-2016 Leif Bloomquist and Alex Burger

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* ESP8266 port by Alex Burger */

/* Written with assistance and code from Greg Alekel and Payton Byrd */

/* ChangeLog
Sept 18th, 2016: Alex Burger
- Change Hayes/Menu selection to variable.

Feb 24th, 2016: Alex Burger
- Initial port of 0.12b5 with Hayes, Menu, but no incoming connections.
- Flow control not working properly.
*/

/* TODO /issues
-Can't connect to hosts on local network, but can connect via default gateway.
-at&f&c1 causes DCD to go high when &f is processed, then low again when &c1 is processed.
-incoming
-baud rate change
-After switching between hayes/menu, WiFi may not reconnect.
-After setting the passphrase and then SSID (*p, *s), WiFi may not connect.  Power off/on works.
-Hayes/menu interferring with each other's settings?

*/

#define MICROVIEW        // define to include MicroView display library and features

//#define DEBUG

// Defining HAYES enables Hayes commands and disables the 1) and 2) menu options for telnet and incoming connections.
// This is required to ensure the compiled code is <= 30,720 bytes 
// Free memory at AT command prompt should be around 300.  At 200, ATI output is garbled.
#define HAYES     // Also define in WiFlyHQ.cpp!


// For system_get_free_heap_size()
//extern "C" {
//#include "user_interface.h"
//}

#ifdef MICROVIEW
//#include <U8g2lib.h>
#include <U8x8lib.h>
#include <SPI.h>
#include <Wire.h>
#endif
#include <elapsedMillis.h>
#include <SoftwareSerial.h>
//#include <WiFlyHQ.h>
#include <EEPROM.h>
//#include <digitalWrite.h>
#include "petscii.h"
#include <ESP8266WiFi.h>

;  // Keep this here to pacify the Arduino pre-processor

#define VERSION "ESP 0.13b2"
// Based on 0.12b5 with modem_loop fix.  

unsigned int BAUD_RATE = 2400;
unsigned char BAUD_RATE_FORCED = '0';

// Configuration 0v3: Wifi Hardware, C64 Software.

// Wifi
// RxD is D0  (Hardware Serial)
// TxD is D1  (Hardware Serial)

#define WIFI_RTS  99
#define WIFI_CTS  99

/*
// ESP8266-07
#define C64_RTS  14
#define C64_CTS  12
#define C64_DCD  13
#define C64_DTR  20 // not available
#define C64_RI   20 // not available
#define C64_DSR  18
#define C64_TxD  1      // Connected to C64 RX pin
#define C64_RxD  3      // Connected to C64 TX pin
*/

/*
// ESP8266-12
#define C64_RTS  14
#define C64_CTS  12
#define C64_DCD  13
#define C64_DTR  4
#define C64_RI   20 // not available
#define C64_DSR  5
#define C64_TxD  1
#define C64_RxD  3
*/

// ESP8266-12 WM
#define C64_RTS  13     // D
#define C64_CTS  14     // K
#define C64_DCD  16     // H
#undef C64_DTR
#undef C64_RI    // not available
#undef C64_DSR  
#define C64_TxD  1      // M
#define C64_RxD  3      // B,C
#define I2C_SDA  4
#define I2C_SCL  2
#define I2C_ADDR 0x78

//SoftwareSerial C64Serial(C64_RxD, C64_TxD);
//HardwareSerial& WifiSerial = Serial;
//WiFly wifly;
// Use WiFiClient class to create TCP connections
WiFiClient wifly;


// TODO - remove for esp8266
#define WIFLY_MODE_WEP 0
#define WIFLY_MODE_WPA 1


//SoftwareSerial C64Serial(C64_RxD, C64_TxD);

HardwareSerial& C64Serial = Serial;

// Telnet Stuff
#define NVT_SE 240
#define NVT_NOP 241
#define NVT_DATAMARK 242
#define NVT_BRK 243
#define NVT_IP 244
#define NVT_AO 245
#define NVT_AYT 246
#define NVT_EC 247
#define NVT_GA 249
#define NVT_SB 250
#define NVT_WILL 251
#define NVT_WONT 252
#define NVT_DO 253
#define NVT_DONT 254
#define NVT_IAC 255

#define NVT_OPT_TRANSMIT_BINARY 0
#define NVT_OPT_ECHO 1
#define NVT_OPT_SUPPRESS_GO_AHEAD 3
#define NVT_OPT_STATUS 5
#define NVT_OPT_RCTE 7
#define NVT_OPT_TIMING_MARK 6
#define NVT_OPT_NAOCRD 10
#define NVT_OPT_TERMINAL_TYPE 24
#define NVT_OPT_NAWS 31
#define NVT_OPT_TERMINAL_SPEED 32
#define NVT_OPT_LINEMODE 34
#define NVT_OPT_X_DISPLAY_LOCATION 35
#define NVT_OPT_ENVIRON 36
#define NVT_OPT_NEW_ENVIRON 39

String lastHost = "";
int lastPort = 23;

char escapeCount = 0;
char wifiEscapeCount = 0;
char lastC64input = 0;
unsigned long escapeTimer = 0;
unsigned long wifiEscapeTimer = 0;
boolean escapeReceived = false;
#define ESCAPE_GUARD_TIME 1000

char autoConnectHost = 0;
int mode_Hayes = 1;    // 0 = Meny, 1 = Hayes

//boolean autoConnectedAtBootAlready = 0;           // We only want to auto-connect once..
#define ADDR_HOST_SIZE              40
#define ADDR_ANSWER_MESSAGE_SIZE    75              // Currently limited by the AT command line buffer size which is 80..
#define ADDR_HOST_ENTRIES           9

// EEPROM Addresses
// Microview has 1K
#define ADDR_PETSCII       0
#define ADDR_HAYES_MENU    1
#define ADDR_BAUD          21        // For manually forcing baud rate
#define ADDR_BAUD_LO       2        // Last set baud rate from auto-detect LO
#define ADDR_BAUD_HI       3        // Last set baud rate from auto-detect HI
#define ADDR_MODEM_ECHO         10
#define ADDR_MODEM_FLOW         11
#define ADDR_MODEM_VERBOSE      12
#define ADDR_MODEM_QUIET        13
#define ADDR_MODEM_S0_AUTOANS   14
#define ADDR_MODEM_S2_ESCAPE    15
//#define ADDR_MODEM_DCD_INVERTED 16
#define ADDR_MODEM_DCD          17
#define ADDR_MODEM_X_RESULT     18
#define ADDR_MODEM_SUP_ERRORS   19
#define ADDR_MODEM_DSR          20

//#define ADDR_WIFI_SSID
//#define ADDR_WIFI_PASS


#define ADDR_HOST_AUTO          99     // Autostart host number
#define ADDR_HOSTS              100    // to 460 with ADDR_HOST_SIZE = 40 and ADDR_HOST_ENTRIES = 9
#define STATIC_PB_ENTRIES       2
//#define ADDR_xxxxx            300
#define ADDR_ANSWER_MESSAGE     800    // to 874 with ADDR_ANSWER_MESSAGE_SIZE = 75

// Hayes variables
//#ifdef HAYES
boolean Modem_isCommandMode = true;
boolean Modem_isRinging = false;
boolean Modem_EchoOn = true;
boolean Modem_VerboseResponses = true;
boolean Modem_QuietMode = false;
boolean Modem_S0_AutoAnswer = false;
byte    Modem_X_Result = 0;
boolean Modem_suppressErrors = false;

#define COMMAND_BUFFER_SIZE  81
char Modem_LastCommandBuffer[COMMAND_BUFFER_SIZE];
char Modem_CommandBuffer[COMMAND_BUFFER_SIZE];
char Modem_LastCommandChar;
boolean Modem_AT_Detected = false;
//#endif    // HAYES
char    Modem_S2_EscapeCharacter = '+';
boolean Modem_isConnected = false;

// Misc Values
String SSID_passphrase;
#define TIMEDOUT  -1
boolean Modem_flowControl = false;   // for &K setting.
boolean Modem_isDcdInverted = true;
boolean Modem_DCDFollowsRemoteCarrier = false;    // &C
byte    Modem_dataSetReady = 0;         // &S
int WiFlyLocalPort = 0;
boolean Modem_isCtsRtsInverted = true;           // Normally true on the C64.  False for commodoreserver 38,400.
boolean isFirstChar = true;
boolean isTelnet = false;
boolean telnetBinaryMode = false;
//#ifdef HAYES
boolean petscii_mode_guess = false;
boolean commodoreServer38k = false;
//#endif
//int max_buffer_size_reached = 0;            // For debugging 1200 baud buffer

/* PETSCII state.  Always use ASCII for Hayes.
To set SSID, user must use ASCII mode.
*/
//#ifndef HAYES
boolean petscii_mode = EEPROM.read(ADDR_PETSCII);
//#endif

// Autoconnect Options
#define AUTO_NONE     0
#define AUTO_HAYES    1
#define AUTO_CSERVER  2
#define AUTO_QLINK    3
#define AUTO_CUSTOM   100   // TODO

#ifdef MICROVIEW
//U8G2_SSD1306_128X64_NONAME_1_SW_I2C u8g2(U8G2_R0, I2C_SCL, I2C_SDA);
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(I2C_SCL, I2C_SDA, U8X8_PIN_NONE);
#endif

// ----------------------------------------------------------
// Arduino Setup Function

void setup() {
   
    EEPROM.begin(1024);
    //ESP.wdtDisable();   
}


void loop()
{
    //init();
#ifdef MICROVIEW
    Wire.begin(I2C_SDA, I2C_SCL);

    /*
    u8g2.begin();
    u8g2.clearDisplay();
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_ncenB10_tr);
        //u8g2.setFont(u8g2_font_ncenB12_tr);
        //u8g2.setFont(u8g2_font_ncenB14_tr);
        //u8g2.setFont(u8g2_font_baby_tr);
        u8g2.drawStr(0, 24, "Wi-Fi Modem");
    } while (u8g2.nextPage() );
    */

    u8x8.begin();
    u8x8.setPowerSave(0);

    u8x8.setFont(u8x8_font_chroma48medium8_r);
    u8x8.clear();
    u8x8.inverse();
    u8x8.drawString(0, 1, "Wi-Fi Modem");
    u8x8.noInverse();
    u8x8.drawString(0, 3, "Firmware by");
    u8x8.drawString(0, 4, "Alex Burger &");
    u8x8.drawString(0, 5, "Leif Bloomquist");
    delay(2000);

#endif // MICROVIEW

    // Always setup pins for flow control
    pinMode(C64_RTS, INPUT);
    pinMode(C64_CTS, OUTPUT);

    //pinMode(WIFI_RTS, INPUT);
    //pinMode(WIFI_CTS, OUTPUT);

    // Force CTS to defaults on both sides to start, so data can get through
    //esp digitalWrite(WIFI_CTS, LOW);
    digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));

    pinMode(C64_DCD, OUTPUT);
    Modem_DCDFollowsRemoteCarrier = EEPROM.read(ADDR_MODEM_DCD);

    if (Modem_DCDFollowsRemoteCarrier) {
        digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
    }
    else {
        digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
    }

    Modem_dataSetReady = EEPROM.read(ADDR_MODEM_DSR);
#ifdef C64_DSR
    if (Modem_dataSetReady == 2)
        pinMode(C64_DSR, INPUT);    // Set as input to make sure it doesn't interfere with UP9600.
                                        // Some computers have issues with 100 ohm resistor pack
#endif

    BAUD_RATE_FORCED = (EEPROM.read(ADDR_BAUD));

    long detectedBaudRate;

    switch (BAUD_RATE_FORCED) {
    case '0':         // Auto-detect

        BAUD_RATE = (EEPROM.read(ADDR_BAUD_LO) * 256 + EEPROM.read(ADDR_BAUD_HI));

        if (BAUD_RATE != 300 && BAUD_RATE != 600 && BAUD_RATE != 1200 && BAUD_RATE != 2400 && BAUD_RATE != 4800 && BAUD_RATE != 9600 && BAUD_RATE != 19200 &&
            BAUD_RATE != 38400 && BAUD_RATE != 57600 && BAUD_RATE != 115200)
            BAUD_RATE = 2400;

        //
        // Baud rate detection
        //
        pinMode(C64_RxD, INPUT);
        digitalWrite(C64_RxD, HIGH);

        Display(("Baud Detection"), true, 0);
    
        detectedBaudRate = detRate(C64_RxD);  // Function finds a standard baudrate of either
                                                   // 1200,2400,4800,9600,14400,19200,28800,38400,57600,115200
                                                   // by having sending circuit send "U" characters.
                                                   // Returns 0 if none or under 1200 baud
                                                   //char temp[20];
                                                   //sprintf(temp, "Baud\ndetected:\n%ld", detectedBaudRate);
                                                   //Display(temp);
        //long detectedBaudRate = BAUD_RATE;
        if (detectedBaudRate == 300 || detectedBaudRate == 600 ||
            detectedBaudRate == 1200 || detectedBaudRate == 2400 || detectedBaudRate == 4800 ||
            detectedBaudRate == 9600 || detectedBaudRate == 19200 || detectedBaudRate == 38400 || 
            detectedBaudRate == 57600 || detectedBaudRate == 115200)
        {
            char temp[6];
            sprintf_P(temp, PSTR("%ld"), detectedBaudRate);
            Display(temp, false, 1);
            delay(3000);

            BAUD_RATE = detectedBaudRate;

            byte a = BAUD_RATE / 256;
            byte b = BAUD_RATE % 256;

            updateEEPROMByte(ADDR_BAUD_LO, a);
            updateEEPROMByte(ADDR_BAUD_HI, b);
        }
    
        //
        // Baud rate detection end
        //
        break;
    
    case '1':         // 1200 baud
        BAUD_RATE = 1200;
        //Display(("1200"), true, 0);
        delay(3000);
        break;

    default:        // 2400 baud
        BAUD_RATE = 2400;
        //Display(("2400"), true, 0);
        delay(3000);
        break;
    }

    C64Serial.begin(BAUD_RATE);
    delay(10);

    //C64Serial.setTimeout(1000);

    C64Serial.println();
    if (mode_Hayes)
        DisplayBoth(("WI-FI INIT..."), true, 0);
    else
        DisplayBoth(("Wi-Fi Init..."), true, 0);

    C64Serial.println();
    C64Serial.println();
    C64Serial.print("Connecting to ");
    C64Serial.println(WiFi.SSID());

    //WiFi.begin(ssid, password);
    WiFi.begin();

    int WiFicounter = 0;
    boolean WiFiConnectSuccess = false;
    while (WiFicounter < 40) {
        delay(500);
        Serial.print(".");
        if (WiFi.status() == WL_CONNECTED) {
            WiFiConnectSuccess = true;
            break;
        }
        WiFicounter++;
    }

    //WiFiClient WifiSerial;

    // Menu or Hayes AT command mode
    mode_Hayes = EEPROM.read(ADDR_HAYES_MENU);
    if (mode_Hayes < 0 || mode_Hayes > 1)
        mode_Hayes = 0;

    C64Serial.println();
    if (WiFiConnectSuccess)
    {
        if(mode_Hayes)
            DisplayBoth(("WI-FI OK!"), true, 0);
        else
            DisplayBoth(("Wi-Fi OK!"), true, 0);
    }
    else
    {
        if (mode_Hayes)
            DisplayBoth(("WI-FI FAILED!"), true, 0);
        else
            DisplayBoth(("Wi-Fi Failed!"), true, 0);
        //RawTerminalMode();
    }

    //C64Serial.println("");
    //C64Serial.println("WiFi connected");
    //C64Serial.println("IP address: ");

    /*configureWiFly();

    if (BAUD_RATE != 2400) {
        delay(1000);
        if (BAUD_RATE == 1200)
            setBaudWiFi(2400);
        else
            setBaudWiFi(BAUD_RATE);
    }*/

    //wifly.stop();

    autoConnectHost = EEPROM.read(ADDR_HOST_AUTO);
//#ifndef HAYES
    if (!mode_Hayes) {
        Modem_flowControl = EEPROM.read(ADDR_MODEM_FLOW);
        HandleAutoStart();
    }
//#endif  // HAYES

    //C64Println();
//#ifdef HAYES
    if (mode_Hayes) {
        //C64Println(F("\r\nCommodore Wi-Fi Modem Hayes Emulation"));
        C64Println(F("\r\COMMODORE WI-FI MODEM HAYES EMULATION"));
        //ShowPETSCIIMode();
        C64Println();
        ShowInfo(true);
        HayesEmulationMode();
    }
    else {
        //#else
        C64Println(F("\r\nCommodore Wi-Fi Modem"));
        C64Println();
        ShowInfo(true);

        while (1)
        {
            Display(("READY."), true, 0);

            // Clear phonebook.  TODO:  On ESP8266-07, 2) menu popped up and disappeared.  ASCII response to garbage?
            /*
            for (int i = 0; i < ADDR_HOST_ENTRIES; i++)
            {
                updateEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE), "\0");
            }
            */

            ShowPETSCIIMode();
            C64Print(F("1. Telnet to host or BBS\r\n"
                "2. Phone Book\r\n"
                "3. Wait for incoming connection\r\n"
                "4. Configuration\r\n"
                "5. Hayes Emulation Mode\r\n"
                "\r\n"
                "Select: "));

            int option = ReadByte(C64Serial);
            C64Serial.println((char)option);

            switch (option)
            {
            case '1':
                DoTelnet();
                break;

            case '2':
                PhoneBook();
                break;

           case '3':
                Incoming();
                break;

            case '4':
                Configuration();
                break;

            case '5':
                mode_Hayes = true;
                updateEEPROMByte(ADDR_HAYES_MENU, mode_Hayes);
                C64Println(F("Restarting in Hayes Emulation mode."));
                C64Println(F("Use AT&M to return to menu mode."));
                C64Println();
                ESP.restart();
                while (1);
                break;

            case '\n':
            case '\r':
            case ' ':
                break;

            case 8:
                SetPETSCIIMode(false);
                break;

            case 20:
                SetPETSCIIMode(true);
                break;

            default:
                C64Println(F("Unknown option, try again"));
                break;
            }
        }
    }
//#endif // HAYES
}

//#ifndef HAYES
void Configuration()
{
    while (true)
    {
        char temp[30];
        C64Print(F("\r\n"
            "Configuration Menu\r\n"
            "\r\n"
            "1. Display Current Configuration\r\n"
            "2. Change SSID\r\n"));

        sprintf_P(temp, PSTR("3. %s flow control"), Modem_flowControl == true ? "Disable" : "Enable");
        C64Println(temp);
        sprintf_P(temp, PSTR("4. %s DCD always on"), Modem_DCDFollowsRemoteCarrier == false ? "Disable" : "Enable");
        C64Println(temp);
        C64Print(F("5. Direct Terminal Mode (Debug)\r\n"
            "6. Return to Main Menu\r\n"
            "\r\nSelect: "));

        int option = ReadByte(C64Serial);
        C64Serial.println((char)option);

        switch (option)
        {
        case '1':
            ShowInfo(false);
            delay(1000);        // Sometimes menu doesn't appear properly after 
                                // showInfo().  May only be at high speeds such as 38400.  Trying this..
            break;

        case '2': ChangeSSID();
            break;

        case '3':
            Modem_flowControl = !Modem_flowControl;
            updateEEPROMByte(ADDR_MODEM_FLOW, Modem_flowControl);
            break;

        case '4':
            Modem_DCDFollowsRemoteCarrier = !Modem_DCDFollowsRemoteCarrier;

            if (Modem_DCDFollowsRemoteCarrier) {
                digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
            }
            else {
                digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
            }

            updateEEPROMByte(ADDR_MODEM_DCD, Modem_DCDFollowsRemoteCarrier);
            break;

        //case '5': RawTerminalMode();
            //return;

        case '6': return;

        case 8:
            SetPETSCIIMode(false);
            continue;

        case 20:
            SetPETSCIIMode(true);
            continue;

        case '\n':
        case '\r':
        case ' ':
            continue;

        default: C64Println(F("Unknown option, try again"));
            continue;
        }
    }
}

void ChangeSSID()
{
    int mode = -1;

    while (true)
    {
        C64Print(F("\r\n"
            "Change SSID\r\n"
            "\r\n"
            "1. WEP\r\n"
            "2. WPA / WPA2\r\n"
            "3. Return to Configuration Menu\r\n"
            "\r\n"
            "Select: "));

        int option = ReadByte(C64Serial);
        C64Serial.println((char)option);

        switch (option)
        {
        case '1': mode = WIFLY_MODE_WEP;
            break;

        case '2': mode = WIFLY_MODE_WPA;
            break;

        case '3': return;

        case 8:
            SetPETSCIIMode(false);
            continue;

        case 20:
            SetPETSCIIMode(true);
            continue;

        case '\n':
        case '\r':
        case ' ':
            continue;

        default: C64Println(F("Unknown option, try again"));
            continue;
        }

        C64Println();
        String input;

        switch (mode)
        {
        case WIFLY_MODE_WEP:
            C64Print(F("Key:"));
            SSID_passphrase = GetInput();
            break;

        case WIFLY_MODE_WPA:
            C64Print(F("Passphrase:"));
            SSID_passphrase = GetInput();
            break;

        default:  // Should never happen
            continue;
        }

        C64Println();
        C64Print(F("SSID:"));
        input = GetInput();

        C64Println(F("\r\nConnecting to network...\r\n"));

        WiFi.begin(input.c_str(), SSID_passphrase.c_str());

        int WiFicounter = 0;
        boolean WiFiConnectSuccess = false;
        while (WiFicounter < 40) {
            delay(500);
            Serial.print(".");
            if (WiFi.status() == WL_CONNECTED) {
                WiFiConnectSuccess = true;
                break;
            }
            WiFicounter++;
        }
        if (WiFiConnectSuccess == false)
        {
            C64Println(F("\r\nError joining network"));
            continue;
        }
        else
        {
            C64Println(F("\r\nSSID Successfully changed"));
            return;
        }
    }
}


void PhoneBook()
{
    while (true)
    {
        char address[ADDR_HOST_SIZE];
        char numString[2];

        C64Println();
        DisplayPhoneBook();
        //C64Print(F("\r\nSelect: #, m to modify, a to set\r\n""auto-start, 0 to go back: "));
        C64Print(F("\r\nSelect: #, m to modify, c to clear all\r\na to set auto-start, 0 to go back: "));

        char addressChar = ReadByte(C64Serial);
        C64Serial.println((char)addressChar);

        if (addressChar == 'm' || addressChar == 'M')
        {
            C64Print(F("\r\nEntry # to modify? (0 to abort): "));

            char addressChar = ReadByte(C64Serial);

            numString[0] = addressChar;
            numString[1] = '\0';
            int phoneBookNumber = atoi(numString);
            if (phoneBookNumber >= 0 && phoneBookNumber <= ADDR_HOST_ENTRIES)
            {
                C64Serial.print(phoneBookNumber);
                switch (phoneBookNumber) {
                case 0:
                    break;

                default:
                    C64Print(F("\r\nEnter address: "));
                    String hostName = GetInput();
                    if (hostName.length() > 0)
                    {
                        updateEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE), hostName);
                    }
                    else
                        updateEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE), "");

                }
            }
        }
        else if (addressChar == 'c' || addressChar == 'C')
        {
            C64Print(F("\r\nAre you sure (y/n)? "));

            char addressChar = ReadByte(C64Serial);

            if (addressChar == 'y' || addressChar == 'Y')
            {
                for (int i = 0; i < ADDR_HOST_ENTRIES; i++)
                {
                    updateEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE), "\0");
                }
            }
        }
        else if (addressChar == 'a' || addressChar == 'A')
        {
            C64Print(F("\r\nEntry # to set to auto-start?\r\n""(0 to disable): "));

            char addressChar = ReadByte(C64Serial);

            numString[0] = addressChar;
            numString[1] = '\0';
            int phoneBookNumber = atoi(numString);
            if (phoneBookNumber >= 0 && phoneBookNumber <= ADDR_HOST_ENTRIES)
            {
                C64Serial.print(phoneBookNumber);
                updateEEPROMByte(ADDR_HOST_AUTO, phoneBookNumber);
            }

        }
        else
        {
            numString[0] = addressChar;
            numString[1] = '\0';
            int phoneBookNumber = atoi(numString);

            if (phoneBookNumber >= 0 && phoneBookNumber <= ADDR_HOST_ENTRIES)
            {
                switch (phoneBookNumber) {
                case 0:
                    return;

                default:
                {
                    strncpy(address, readEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE)).c_str(), ADDR_HOST_SIZE);
                    removeSpaces(address);
                    Modem_Dialout(address);
                }

                break;
                }

            }
        }
    }
}

//#endif  // HAYES

// ----------------------------------------------------------
// MicroView Display helpers


void Display(String message, boolean clear, int line)
{
#ifdef MICROVIEW
    u8x8.setFont(u8x8_font_chroma48medium8_r);
    if (clear)
        u8x8.clear();
    if (line != -1)
        u8x8.setCursor(0, line);
    u8x8.print(message);
#endif
}

/*void Display(String message, boolean clear, int line)
{
#ifdef MICROVIEW
    u8x8.setFont(u8x8_font_chroma48medium8_r);
    if (clear)
        u8x8.clear();
    u8x8.drawString(0, line, message.c_str());
#endif
}*/

// Pointer version.  Does not work with F("") or PSTR("").  Use with sprintf and sprintf_P
void DisplayP(const char *message, boolean clear, int line)
{
#ifdef MICROVIEW
    u8x8.setFont(u8x8_font_chroma48medium8_r);
    if (clear)
        u8x8.clear();
    if (line != -1)
        u8x8.setCursor(0, line);
    u8x8.print(message);
#endif
}

void DisplayBoth(String message, boolean clear, int line)
{
    C64Println(message);
    //message.replace(' ', '\n');
    Display(message, clear, line);
}

// Pointer version.  Does not work with F("") or PSTR("").  Use with sprintf and sprintf_P
void DisplayBothP(const char *message, boolean clear, int line)
{
    String temp = message;
    C64PrintlnP(temp.c_str());
    //temp.replace(' ', '\n');
    DisplayP(temp.c_str(), clear, line);
}


// ----------------------------------------------------------
// Wrappers that always print correctly for ASCII/PETSCII

void C64Print(String message)
{
//#ifdef HAYES
    if (mode_Hayes)
        C64Serial.print(message);
    else {
//#else
        if (petscii_mode)
        {
            C64Serial.print(petscii::ToPETSCII(message.c_str()));
        }
        else
        {
            C64Serial.print(message);
        }
    }
//#endif
}

void C64Println(String message)
{
    C64Print(message);
    C64Serial.println();
}

// Pointer version.  Does not work with F("") or PSTR("").  Use with sprintf and sprintf_P
void C64PrintP(const char *message)
{
//#ifdef HAYES
    if (mode_Hayes)
        C64Serial.print(message);
    else {
        //#else
        if (petscii_mode)
        {
            C64Serial.print(petscii::ToPETSCII(message));
        }
        else
        {
            C64Serial.print(message);
        }
    }
//#endif
}

// Pointer version.  Does not work with F("") or PSTR("").  Use with sprintf and sprintf_P
void C64PrintlnP(const char *message)
{
    C64PrintP(message);
    C64Serial.println();
}

void C64Println()
{
    C64Serial.println();
}

//#ifndef HAYES
void ShowPETSCIIMode()
{
    C64Println();

    if (petscii_mode)
    {
        const char message[] =
        {
            petscii::CG_RED, 'p',
            petscii::CG_ORG, 'e',
            petscii::CG_YEL, 't',
            petscii::CG_GRN, 's',
            petscii::CG_LBL, 'c',
            petscii::CG_CYN, 'i',
            petscii::CG_PUR, 'i',
            petscii::CG_WHT, ' ', 'm', 'O', 'D', 'E', '!',
            petscii::CG_GR3, '\0'
        };
        C64Serial.print(message);

    }
    else
    {
        C64Serial.print(F("ASCII Mode"));
    }

    //#ifndef HAYES
    C64Println(F(" (Del to switch)"));
    //#endif  // HAYES

    C64Println();
}

void SetPETSCIIMode(boolean mode)
{
    petscii_mode = mode;
    updateEEPROMByte(ADDR_PETSCII, petscii_mode);
}
//#endif  // HAYES


// ----------------------------------------------------------
// User Input Handling

boolean IsBackSpace(char c)
{
    if ((c == 8) || (c == 20) || (c == 127))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//#ifndef HAYES
String GetInput()
{
    String temp = GetInput_Raw();
    temp.trim();

    if (petscii_mode)  // Input came in PETSCII form
    {
        return petscii::ToASCII(temp.c_str());
    }

    return temp;
}

String GetInput_Raw()
{
    char temp[50];

    int max_length = sizeof(temp);

    int i = 0; // Input buffer pointer
    char key;

    while (true)
    {
        key = ReadByte(C64Serial);  // Read in one character

        if (!IsBackSpace(key))  // Handle character, if not backspace
        {
            temp[i] = key;
            C64Serial.write(key);    // Echo key press back to the user

            if (((int)key == 13) || (i >= (max_length - 1)))   // The 13 represents enter key.
            {
                temp[i] = 0; // Terminate the string with 0.
                return String(temp);
            }
            i++;
        }
        else     // Backspace
        {
            if (i > 0)
            {
                C64Serial.write(key);
                i--;
            }
        }

        // Make sure didn't go negative
        if (i < 0) i = 0;
    }
}
//#endif        // HAYES

// ----------------------------------------------------------
// Show Configuration

void ShowInfo(boolean powerup)
{
    char mac[20];
    char ip[20];
    char ssid[20];
    C64Println();
    //C64Serial.print(freeRam());
    //C64Println();

    //wifly.getPort();    // Sometimes the first time contains garbage..
    //wifly.getMAC(mac, 20);
    //wifly.getIP(ip, 20);
    //wifly.getSSID(ssid, 20);
    //WiFlyLocalPort = wifly.getPort();   // Port WiFly listens on.  0 = disabled.

//#ifdef HAYES
    if (mode_Hayes) {
        yield();  // For 300 baud
        //C64Println();
        //C64Print(F("MAC Address: "));    C64Println(mac);
        C64Print(F("IP Address:  "));    Serial.println(WiFi.localIP());
        yield();  // For 300 baud
        C64Print(F("IP Subnet:   "));    Serial.println(WiFi.subnetMask());
        yield();  // For 300 baud
        C64Print(F("IP Gateway:  "));    Serial.println(WiFi.gatewayIP());
        yield();  // For 300 baud
        C64Print(F("Wi-Fi SSID:  "));    Serial.println(WiFi.SSID());
        yield();  // For 300 baud
        C64Print(F("MAC Address: "));    Serial.println(WiFi.macAddress());
        yield();  // For 300 baud
        C64Print(F("DNS IP:      "));    Serial.println(WiFi.dnsIP());
        yield();  // For 300 baud
        C64Print(F("Hostname:    "));    Serial.println(WiFi.hostname());
        yield();  // For 300 baud
        C64Print(F("Firmware:    "));    C64Println(VERSION);
        //C64Print(F("Listen port: "));    C64Serial.print(WiFlyLocalPort); C64Serial.println();
        yield();  // For 300 baud

        if (!powerup) {
            char at_settings[40];
            //sprintf_P(at_settings, PSTR("ATE%dQ%dV%d&C%d&K%dS0=%d"), Modem_EchoOn, Modem_QuietMode, Modem_VerboseResponses, Modem_DCDFollowsRemoteCarrier, Modem_flowControl, Modem_S0_AutoAnswer);
            sprintf_P(at_settings, PSTR("\r\n E%d Q%d V%d &C%d X%d &K%d &S%d *B%c\r\n S0=%d S2=%d S99=%d"),
                Modem_EchoOn, Modem_QuietMode, Modem_VerboseResponses,
                Modem_DCDFollowsRemoteCarrier, Modem_X_Result, Modem_flowControl,
                Modem_dataSetReady, BAUD_RATE_FORCED,
                Modem_S0_AutoAnswer, (int)Modem_S2_EscapeCharacter,
                Modem_suppressErrors);
            C64Print(F("CURRENT INIT:"));    C64Println(at_settings);
            yield();  // For 300 baud
                      //sprintf_P(at_settings, PSTR("ATE%dQ%dV%d&C%d&K%dS0=%d"),EEPROM.read(ADDR_MODEM_ECHO),EEPROM.read(ADDR_MODEM_QUIET),EEPROM.read(ADDR_MODEM_VERBOSE),EEPROM.read(ADDR_MODEM_DCD),EEPROM.read(ADDR_MODEM_FLOW),EEPROM.read(ADDR_MODEM_S0_AUTOANS));
            sprintf_P(at_settings, PSTR("\r\n E%d Q%d V%d &C%d X%d &K%d &S%d *B%c\r\n S0=%d S2=%d S99=%d"),
                EEPROM.read(ADDR_MODEM_ECHO), EEPROM.read(ADDR_MODEM_QUIET), EEPROM.read(ADDR_MODEM_VERBOSE),
                EEPROM.read(ADDR_MODEM_DCD), EEPROM.read(ADDR_MODEM_X_RESULT), EEPROM.read(ADDR_MODEM_FLOW),
                EEPROM.read(ADDR_MODEM_DSR), EEPROM.read(ADDR_BAUD),
                EEPROM.read(ADDR_MODEM_S0_AUTOANS), EEPROM.read(ADDR_MODEM_S2_ESCAPE),
                EEPROM.read(ADDR_MODEM_SUP_ERRORS));
            C64Print(F("SAVED INIT:  "));    C64Println(at_settings);
            yield();  // For 300 baud
        }        
    }
    else {
        //#else
                                                //C64Println();
            //C64Print(F("MAC Address: "));    C64Println(mac);
        C64Print(F("IP Address:  "));    Serial.println(WiFi.localIP());
        C64Print(F("IP Subnet:   "));    Serial.println(WiFi.subnetMask());
        yield();  // For 300 baud
        C64Print(F("IP Gateway:  "));    Serial.println(WiFi.gatewayIP());
        C64Print(F("Wi-Fi SSID:  "));    Serial.println(WiFi.SSID());
        yield();  // For 300 baud
        C64Print(F("MAC Address: "));    Serial.println(WiFi.macAddress());
        C64Print(F("DNS IP:      "));    Serial.println(WiFi.dnsIP());
        yield();  // For 300 baud
        C64Print(F("Hostanme:    "));    Serial.println(WiFi.hostname());
        C64Print(F("Firmware:    "));    C64Println(VERSION);
        //C64Print(F("Listen port: "));    C64Serial.print(WiFlyLocalPort); C64Serial.println();
    }
    yield();  // For 300 baud
//#endif

//#ifndef HAYES
#ifdef MICROVIEW    
    if (powerup)
    {
        char temp[40];

        yield();  // For 300 baud

        u8x8.clearDisplay();
        u8x8.setCursor(0, 0);
        u8x8.print("Firmware:");
        u8x8.setCursor(1, 1);
        u8x8.print(VERSION);

        u8x8.setCursor(0, 2);
        u8x8.print("Baud Rate:");
        u8x8.setCursor(1, 3);
        u8x8.print(BAUD_RATE);

        u8x8.setCursor(0, 4);
        u8x8.print("IP Address:");
        u8x8.setCursor(1, 5);
        u8x8.print(WiFi.localIP());

        u8x8.setCursor(0, 6);
        u8x8.print("SSID:");
        u8x8.setCursor(1, 7);
        u8x8.print(WiFi.SSID());

        delay(3000);
    }
#endif  // MICROVIEW    
//#endif  // HAYES
}

//#ifndef HAYES
// ----------------------------------------------------------
// Simple Incoming connection handling

void Incoming()
{
    //int localport = WiFlyLocalPort;
    int localport = 23;

    C64Print(F("\r\nIncoming port ("));
    C64Serial.print(localport);
    C64Print(F("): "));

    String strport = GetInput();

    if (strport.length() > 0)
    {
        localport = strport.toInt();
        /*if (setLocalPort(localport))
            while (1);*/
    }

    WiFlyLocalPort = localport;

    C64Print(F("\r\nWaiting for connection on port "));
    C64Serial.println(WiFlyLocalPort);
    C64Print(F("\r\nIP address: "));
    C64Serial.println(WiFi.localIP());

    WiFiServer server(WiFlyLocalPort);
    server.begin();
    server.setNoDelay(true);
    C64Serial.println("Server started");
    
    while (1)
    {
        yield();
        // Force close any connections that were made before we started listening, as
        // the WiFly is always listening and accepting connections if a local port
        // is defined.  
        //wifly.closeForce();
        wifly.stop();

        // Idle here until connected or cancelled
        while (1)
        {
            /*if (server.hasClient()) {
                if (!wifly || !wifly.connected()) {
                    if (wifly) wifly.stop();
                    wifly = server.available();
                    break;
                }
                else {
                    server.available().stop();
                }
            }*/

            wifly = server.available();
            if (wifly) {
                break;
            }
            if (C64Serial.available() > 0)  // Key hit
            {
                C64Serial.read();  // Eat Character
                C64Println(F("Cancelled"));
                return;
            }
            delay(1);
        }

        //wifly = server.available();
        wifly.flush();

        C64Println(F("Incoming Connection"));
        wifly.println(F("CONNECTING..."));
        //CheckTelnet();
        TerminalMode();
        server.stop();
        wifly.stop();
        break;
    }
}


// ----------------------------------------------------------
// Telnet Handling
void DoTelnet()
{
    int port = lastPort;

    C64Print(F("\r\nTelnet host ("));
    C64Print(lastHost);
    C64Print(F("): "));
    String hostName = GetInput();

    if (hostName.length() > 0)
    {
        port = getPort();

        lastHost = hostName;
        lastPort = port;

        Connect(hostName, port, false);
    }
    else
    {
        if (lastHost.length() > 0)
        {
            port = getPort();

            lastPort = port;
            Connect(lastHost, port, false);
        }
        else
        {
            return;
        }
    }
}

int getPort(void)
{
    C64Print(F("\r\nPort ("));
    C64Serial.print(lastPort);
    C64Print(F("): "));

    String strport = GetInput();

    if (strport.length() > 0)
    {
        return(strport.toInt());
    }
    else
    {
        return(lastPort);
    }
}
//#endif // HAYES

void Connect(String host, int port, boolean raw)
{
//#ifdef HAYES
    if (host == F("CS38")) {
        host = F("www.commodoreserver.com");
        port = 1541;
        commodoreServer38k = true;

        if (BAUD_RATE != 38400)
        {
            BAUD_RATE = 38400;
            C64Serial.flush();
            delay(1000);
            C64Serial.flush();
            C64Serial.end();
            C64Serial.begin(BAUD_RATE);
            //C64Serial.setTimeout(1000);
        }
        Modem_isCtsRtsInverted = false;
        Modem_flowControl = true;
        delay(1000);
    }
//#endif    // HAYES

    char temp[50];
    //sprintf_P(temp, PSTR("\r\nConnecting to %s"), host.c_str());
    snprintf_P(temp, (size_t)sizeof(temp), PSTR("\r\nConnecting to %s"), host.c_str());
//#ifdef HAYES
    if (mode_Hayes)
        DisplayP(temp, true, 1);
//#else
    DisplayBothP(temp, true, 1);
//#endif

    // Do a DNS lookup to get the IP address. 4 Lookup has a 5 second timeout.
    /*char ip[16];
    if (!wifly.getHostByName(host.c_str(), ip, sizeof(ip))) {
        DisplayBoth(("Could not resolve DNS.  Connect Failed!"));
        delay(1000);
#ifdef HAYES
        if ((Modem_X_Result == 2) || (Modem_X_Result >= 4))  // >=2 but not 3 = Send NO DIALTONE           
            Modem_PrintResponse(6, ("NO DIALTONE"));
        else
            //Modem_Disconnect(true);
            Modem_PrintResponse(3, ("NO CARRIER"));
#endif
        return;
    }*/

    //boolean ok = wifly.open(ip, port);
    //IPAddress ip(192, 168, 4, 145);
    //WiFiClient wifly2;
    //const char* host2 = "192.168.4.145";
    //const char* host2 = "bbs.jammingsignal.com";
    //const char* host2 = "cib.dyndns.org";
    //const char* host2 = "batcave.fragit.net";
    //const int httpPort = 6502;

    char host2[50];
    snprintf(host2, (size_t)sizeof(host2), "%s", host.c_str());

    boolean ok = wifly.connect(host2, port);

    if (ok)
    {
        //sprintf_P(temp, PSTR("Connected to %s"), host.c_str());
        snprintf_P(temp, (size_t)sizeof(temp), PSTR("Connected to %s"), host.c_str());
//#ifdef HAYES
        if (mode_Hayes)
            DisplayP(temp, true, 1);
        else
//#else
            DisplayBothP(temp, true, 1);
//#endif
        //        if (Modem_DCDFollowsRemoteCarrier)
        //            digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
    }
    else
    {
//#ifdef HAYES
        if (mode_Hayes) {
            Display(("Connect Failed!"), true, 1);
            delay(1000);
            if (Modem_X_Result >= 3)
                Modem_PrintResponse(7, ("BUSY"));
            else
                //Modem_Disconnect(true);
                Modem_PrintResponse(3, ("NO CARRIER"));
        }
        else {
//#else
            DisplayBoth(("Connect Failed!"), true, 0);
//#endif
        }
        //        if (Modem_DCDFollowsRemoteCarrier)
        //            digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
        return;
    }

//#ifdef HAYES
    if (mode_Hayes)
        Modem_Connected(false);
//#else
    else {
        if (Modem_DCDFollowsRemoteCarrier)
            digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
        TerminalMode();
    }
//#endif
}


//#ifndef HAYES
void TerminalMode()
{
    int data;
    char buffer[10];
    int buffer_index = 0;
    int buffer_bytes = 0;
    isFirstChar = true;
    //int max_buffer_size_reached = 0;
    //int heap;
    //char temp[50];

    //while (wifly.available() != -1) // -1 means closed
    while (wifly.connected()) // -1 means closed
    {
        //heap = system_get_free_heap_size();
        
        //sprintf_P(temp, "Heap:%d\r\n", heap);       
        //C64Serial.write(temp);
        
        while (wifly.available() > 0)
        {
            int data = wifly.read();
            yield();

            // If first character back from remote side is NVT_IAC, we have a telnet connection.
            if (isFirstChar) {
                if (data == NVT_IAC)
                {
                    isTelnet = true;
                }
                else
                {
                    isTelnet = false;
                }
            }

            if (data == NVT_IAC && isTelnet)
            {
                {
                    if (CheckTelnetInline())
                        C64Serial.write(NVT_IAC);
                }
            }
            else
            {
                {
                    DoFlowControlModemToC64();
                    C64Serial.write(data);
                }
            }
        }

        while (C64Serial.available() > 0)
        {
            processC64Inbound();
        }

        // Alternate check for open/closed state
        if (!wifly.connected() || escapeReceived)
        {
            escapeReceived = 0;
            break;
        }
    }
    wifly.stop();

//#ifdef HAYES          
    //Modem_Disconnect(true);
//#else
    DisplayBoth(("Connection closed"), true, 0);
//#endif
    if (Modem_DCDFollowsRemoteCarrier)
        digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
    //ESP.wdtEnable();
}

//#endif  // HAYES

// Raw Terminal Mode.  There is no escape.
/*void RawTerminalMode()
{
    char temp[50];

    bool changed = false;
    long rnxv_chars = 0;
    long c64_chars = 0;
    long c64_rts_count = 0;      //  !!!! Note, this isn't actually incremented

    C64Println(F("*** Terminal Mode (Debug) ***"));

    while (true)
    {
        while (wifly.available() > 0)
        {
            rnxv_chars++;
            DoFlowControlModemToC64();

            C64Serial.write(wifly.read());
            changed = true;
        }

        while (C64Serial.available() > 0)
        {
            c64_chars++;
            wifly.write(C64Serial.read());
            changed = true;
        }

        if (changed)
        {
            if (Modem_flowControl)
            {
                sprintf_P(temp, PSTR("Wifi:%ld\n\nC64: %ld\n\nRTS: %ld"), rnxv_chars, c64_chars, c64_rts_count);
            }
            else
            {
                sprintf_P(temp, PSTR("Wifi:%ld\n\nC64: %ld"), rnxv_chars, c64_chars);
            }
            Display(temp);
        }
    }
}*/

inline void DoFlowControlModemToC64()
{
    if (Modem_flowControl)
    {
        // Check that C64 is ready to receive
        //while (digitalRead(C64_RTS == LOW))   // If not...  C64 RTS and CTS are inverted.
        while (digitalRead(C64_RTS) == (Modem_isCtsRtsInverted ? LOW : HIGH))
        {
            yield();
            //delay(5);
            //esp digitalWrite(WIFI_CTS, HIGH);     // ..stop data from Wi-Fi and wait
            ;
        }
        //esp digitalWrite(WIFI_CTS, LOW);
    }
}
inline void DoFlowControlC64ToModem()
{
    if (Modem_flowControl)
    {
        // Check that C64 is ready to receive
        while (digitalRead(WIFI_RTS) == HIGH)   // If not...
        {
            digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? LOW : HIGH));  // ..stop data from C64 and wait
                                                                               //digitalWrite(C64_CTS, LOW);     // ..stop data from C64 and wait
            yield();
        }
        digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));
        //digitalWrite(C64_CTS, HIGH);
    }
}

// Inquire host for telnet parameters / negotiate telnet parameters with host
// Returns true if in binary mode and modem should pass on a 255 byte from remote host to C64

/*boolean CheckTelnetInline()
{
    return false;
}*/

boolean CheckTelnetInline()
{
    int inpint, verbint, optint;                        //    telnet parameters as integers

    yield();

                                                        // First time through
    if (isFirstChar) {
        SendTelnetParameters();                         // Start off with negotiating
        isFirstChar = false;
    }

    verbint = ReadByte(wifly);                          // receive negotiation verb character

    if (verbint == NVT_IAC && telnetBinaryMode)
    {
        return true;                                    // Received two NVT_IAC's so treat as single 255 data
    }

    switch (verbint) {                                  // evaluate negotiation verb character
    case NVT_WILL:                                      // if negotiation verb character is 251 (will)or
    case NVT_DO:                                        // if negotiation verb character is 253 (do) or
        optint = ReadByte(wifly);                       // receive negotiation option character

        switch (optint) {

        case NVT_OPT_SUPPRESS_GO_AHEAD:                 // if negotiation option character is 3 (suppress - go - ahead)
            SendTelnetDoWill(verbint, optint);
            break;

        case NVT_OPT_TRANSMIT_BINARY:                   // if negotiation option character is 0 (binary data)
            SendTelnetDoWill(verbint, optint);
            telnetBinaryMode = true;
            break;

        default:                                        // if negotiation option character is none of the above(all others)
            SendTelnetDontWont(verbint, optint);
            break;                                      //  break the routine
        }
        break;
    case NVT_WONT:                                      // if negotiation verb character is 252 (wont)or
    case NVT_DONT:                                      // if negotiation verb character is 254 (dont)
        optint = ReadByte(wifly);                       // receive negotiation option character

        switch (optint) {

        case NVT_OPT_TRANSMIT_BINARY:                   // if negotiation option character is 0 (binary data)
            SendTelnetDontWont(verbint, optint);
            telnetBinaryMode = false;
            break;

        default:                                        // if negotiation option character is none of the above(all others)
            SendTelnetDontWont(verbint, optint);
            break;                                      //  break the routine
        }
        break;
    case NVT_IAC:                                       // Ignore second IAC/255 if we are in BINARY mode
    default:
        ;
    }
    return false;
}

void SendTelnetDoWill(int verbint, int optint)
{
    wifly.write(NVT_IAC);                               // send character 255 (start negotiation)
    wifly.write(verbint == NVT_DO ? NVT_DO : NVT_WILL); // send character 253  (do) if negotiation verb character was 253 (do) else send character 251 (will)
    wifly.write((int16_t)optint);
}

void SendTelnetDontWont(int verbint, int optint)
{
    wifly.write(NVT_IAC);                               // send character 255   (start negotiation)
    wifly.write(verbint == NVT_DO ? NVT_WONT : NVT_DONT);    // send character 252   (wont) if negotiation verb character was 253 (do) else send character254 (dont)
    wifly.write((int16_t)optint);
}

void SendTelnetParameters()
{
    wifly.write(NVT_IAC);                               // send character 255 (start negotiation) 
    wifly.write(NVT_DONT);                              // send character 254 (dont)
    wifly.write(34);                                    // linemode

    wifly.write(NVT_IAC);                               // send character 255 (start negotiation)
    wifly.write(NVT_DONT);                              // send character 253 (do)
    wifly.write(1);                                     // echo
}



// ----------------------------------------------------------
// Helper functions for read/peek

int ReadByte(Stream& in)
{
    while (in.available() == 0) {}
    return in.read();
}

// Peek at next byte.  Returns byte (as a int, via Stream::peek()) or TIMEDOUT (-1) if timed out

int PeekByte(Stream& in, unsigned int timeout)
{
    elapsedMillis timeElapsed = 0;

    while (in.available() == 0)
    {
        if (timeElapsed > timeout)
        {
            return TIMEDOUT;
        }
    }
    return in.peek();
}

//#ifndef HAYES
void HandleAutoStart()
{
    // Display chosen action

    if (autoConnectHost >= 1 && autoConnectHost <= ADDR_HOST_ENTRIES)
    {
        // Phonebook dial
        char address[ADDR_HOST_SIZE];

        strncpy(address, readEEPROMPhoneBook(ADDR_HOSTS + ((autoConnectHost - 1) * ADDR_HOST_SIZE)).c_str(), ADDR_HOST_SIZE);

        C64Print(F("Auto-connecting to:\r\n"));
        C64Println(address);

        C64Println(F("\r\nPress any key to cancel..."));
        // Wait for user to cancel

        int option = PeekByte(C64Serial, 2000);

        if (option != TIMEDOUT)   // Key pressed
        {
            ReadByte(C64Serial);    // eat character
            return;
        }

        Modem_Dialout(address);
    }
}
//#endif  // HAYES

//#ifdef HAYES
// ----------------------------------------------------------
// Hayes Emulation
// Portions of this code are adapted from Payton Byrd's Hayesduino - thanks!

void HayesEmulationMode()
{
#ifdef C64_DSR    
    Modem_DSR_Set();
#endif

#ifdef C64_RI
    pinMode(C64_RI, OUTPUT);
    digitalWrite(C64_RI, LOW);
#endif

#ifdef C64_DTR
    pinMode(C64_DTR, INPUT);
#endif
    //pinMode(C64_DCD, OUTPUT);     // Moved to top of main()
    pinMode(C64_RTS, INPUT);

    /* Already done in main()
    if (Modem_DCDFollowsRemoteCarrier == true)
    digitalWrite(C64_DCD, Modem_ToggleCarrier(false));*/

    Modem_LoadDefaults(true);

    Modem_LoadSavedSettings();

    Modem_ResetCommandBuffer();

    delay(1000);            //  Sometimes text after ShowInfo at boot doesn't appear.  Trying this..
    C64Println();

    if (autoConnectHost >= 1 && autoConnectHost <= ADDR_HOST_ENTRIES)
    {
        // Phonebook dial
        char address[ADDR_HOST_SIZE];

        strncpy(address, readEEPROMPhoneBook(ADDR_HOSTS + ((autoConnectHost - 1) * ADDR_HOST_SIZE)).c_str(), ADDR_HOST_SIZE);

        C64Print(F("Auto-connecting to:\r\n"));
        C64Println(address);

        C64Println(F("\r\nPress any key to cancel..."));
        // Wait for user to cancel

        int option = PeekByte(C64Serial, 2000);

        if (option != TIMEDOUT)   // Key pressed
        {
            ReadByte(C64Serial);    // eat character
            Display(("OK"), true, 0);
            C64Serial.print(F("OK"));
            C64Serial.println();
        }
        else
            Modem_Dialout(address);
    }
    else
    {
        Display(("OK"), true, 0);
        C64Serial.print(("OK"));
        C64Serial.println();
    }

    while (true)
        Modem_Loop();
}


inline void Modem_PrintOK()
{
    Modem_PrintResponse(0, ("OK"));
}

inline void Modem_PrintERROR()
{
    Modem_PrintResponse(4, ("ERROR"));
}

/* Modem response codes should be in ASCII.  Do not translate to PETSCII. */
//void Modem_PrintResponse(byte code, const __FlashStringHelper * msg)
void Modem_PrintResponse(byte code, String msg)
{
    //C64Println();  // Moved to top of Modem_ProcessCommandBuffer() so that 
    // commands such as ati, at&pb? etc also get get an extra \r\n

    /* ASCII upper = 0x41-0x5a
    ASCII lower = 0x61-0x7a
    PETSCII upper = 0xc1-0xda (second uppercase area)
    PETSCII lower = 0x41-0x5a

    AT from C64 is 0xc1,0xd4.  A real modem will shift the response to match (-0x41 + 0xc1)
    so that if a C64 sends At, the modem will responsd with Ok.  Other examples:
    AT - OK
    aT - oK
    At - Ok
    AT - OK
    AtDt5551212 - No caRrier   Note: Not sure why R is capitalized.  Not implementing here..
    ATdt5551212 - NO CARRIER   Note:  If AT is all upper, result is all upper
    atDT5551212 - no carrier   Note:  If AT is all lower, result is all lower
    */

    if (!Modem_QuietMode)
    {
        if (Modem_VerboseResponses)
        {
            if (petscii_mode_guess)
            {
                // Start with \r\n as seen with real modems.
                C64Serial.print((char)0x8d);
                C64Serial.print((char)0x8a);

                // If AT is PETSCII uppercase, the entire response is shifted -0x41 + 0xc1 (including newline and return!)
                // Newline/return = 0x8d / 0x8a

                const char *p = (const char*)msg.c_str();
                size_t n = 0;
                while (1) {
                    unsigned char c = (*p++);
                    if (c == 0) break;
                    if (c < 0x30 || c > 0x39)
                        C64Serial.print((char)(c + -0x41 + 0xc1));
                    else
                        C64Serial.print((char)c);
                }

                C64Serial.print((char)0x8d);
                C64Serial.print((char)0x8a);
            }
            else {
                // Start with \r\n as seen with real modems.
                C64Println();
                C64Println(msg);
            }
        }
        else
        {
            if (petscii_mode_guess)
            {
                C64Serial.print(code);
                C64Serial.print((char)0x8d);
            }
            else {
                C64Serial.print(code);
                C64Serial.print("\r");
            }
        }
    }

    // Always show verbose version on OLED, underneath command
#ifdef MICROVIEW
    /*uView.println();
    uView.println(msg);
    uView.display();*/

    Display(msg, false, 2);
#endif
}

void Modem_ResetCommandBuffer()
{
    memset(Modem_CommandBuffer, 0, COMMAND_BUFFER_SIZE);
    Modem_AT_Detected = false;
}


void Modem_LoadDefaults(boolean booting)
{
    Modem_isCommandMode = true;
    Modem_isConnected = false;
    Modem_isRinging = false;
    Modem_EchoOn = true;
    //Modem_SetEcho(true);
    Modem_VerboseResponses = true;
    Modem_QuietMode = false;
    Modem_S0_AutoAnswer = false;
    Modem_S2_EscapeCharacter = '+';
    //Modem_isDcdInverted = false;
    Modem_flowControl = false;
    Modem_DCDFollowsRemoteCarrier = false;
    Modem_suppressErrors = false;

    Modem_DCD_Set();

    if (!booting)
    {
        Modem_dataSetReady = 0;
#ifdef C64_DSR
        Modem_DSR_Set();
#endif
    }
}

void Modem_LoadSavedSettings(void)
{
    // Load saved settings
    Modem_EchoOn = EEPROM.read(ADDR_MODEM_ECHO);
    Modem_flowControl = EEPROM.read(ADDR_MODEM_FLOW);
    Modem_VerboseResponses = EEPROM.read(ADDR_MODEM_VERBOSE);
    Modem_QuietMode = EEPROM.read(ADDR_MODEM_QUIET);
    Modem_S0_AutoAnswer = EEPROM.read(ADDR_MODEM_S0_AUTOANS);
    Modem_S2_EscapeCharacter = EEPROM.read(ADDR_MODEM_S2_ESCAPE);
    //Modem_isDcdInverted = EEPROM.read(ADDR_MODEM_DCD_INVERTED);
    Modem_DCDFollowsRemoteCarrier = EEPROM.read(ADDR_MODEM_DCD);
    Modem_X_Result = EEPROM.read(ADDR_MODEM_X_RESULT);
    Modem_suppressErrors = EEPROM.read(ADDR_MODEM_SUP_ERRORS);
    Modem_dataSetReady = EEPROM.read(ADDR_MODEM_DSR);

    Modem_DCD_Set();
#ifdef C64_DSR
    Modem_DSR_Set();
#endif

}

// Only used for at&f and atz commands
void Modem_DCD_Set(void)
{
    if (Modem_DCDFollowsRemoteCarrier == false)
    {
        digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
    }
    else {
        if (Modem_isConnected) {
            digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
        }
        else {
            digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
        }
    }
}

void Modem_Disconnect(boolean printNoCarrier)
{
    //char temp[15];
    Modem_isCommandMode = true;
    Modem_isConnected = false;
    Modem_isRinging = false;
    commodoreServer38k = false;

#ifdef MICROVIEW
    // Erase MicroView screen as NO CARRIER may not fit after RING, CONNECT etc.
    // u8g2.clearDisplay();
#endif

    //if (wifly.available() == -1)
        wifly.stop();
    //else
        //wifly.closeForce();      // Incoming connections need to be force closed.  close()
                                 // does not work because WiFlyHQ.cpp connected variable is
                                 // not set for incoming connections.

    if (printNoCarrier)
        Modem_PrintResponse(3, ("NO CARRIER"));

    if (Modem_DCDFollowsRemoteCarrier)
        digitalWrite(C64_DCD, Modem_ToggleCarrier(false));

#ifdef C64_DSR
    if (Modem_dataSetReady == 1)
        digitalWrite(C64_DSR, HIGH);
#endif
}

// Validate and handle AT sequence  (A/ was handled already)
void Modem_ProcessCommandBuffer()
{
    //boolean petscii_mode_guess = false;
    byte errors = 0;
    //boolean dialed_out = 0;
    // Phonebook dial
    char numString[2];
    char address[ADDR_HOST_SIZE];
    int phoneBookNumber;
    boolean suppressOkError = false;
    char atCommandFirstChars[] = "AEHIQVZ&*RSD\r\n";   // Used for AT commands that have variable length values such as ATS2=45, ATS2=123
    char tempAsciiValue[3];

    // Simple PETSCII/ASCII detection   
    if ((((unsigned char)Modem_CommandBuffer[0] == 0xc1) && ((unsigned char)Modem_CommandBuffer[1] == 0xd4)))
        petscii_mode_guess = true;
    else
        petscii_mode_guess = false;

    // Used for a/ and also for setting SSID, PASS, KEY as they require upper/lower
    strcpy(Modem_LastCommandBuffer, Modem_CommandBuffer);

    // Force uppercase for consistency 

    for (int i = 0; i < strlen(Modem_CommandBuffer); i++)
    {
        Modem_CommandBuffer[i] = toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[i]));
    }

    Display(Modem_CommandBuffer, true, 0);

    // Define auto-start phone book entry
    if (strncmp(Modem_CommandBuffer, ("AT&PBAUTO="), 10) == 0)
    {
        char numString[2];
        numString[0] = Modem_CommandBuffer[10];
        numString[1] = '\0';

        int phoneBookNumber = atoi(numString);
        if (phoneBookNumber >= 0 && phoneBookNumber <= ADDR_HOST_ENTRIES)
        {
            updateEEPROMByte(ADDR_HOST_AUTO, phoneBookNumber);
        }
        else
            errors++;
    }
    // Set listening TCP port
    /*else if (strncmp(Modem_CommandBuffer, ("AT&PORT="), 8) == 0)
    {
        char numString[6];

        int localport = atoi(&Modem_CommandBuffer[8]);
        if (localport >= 0 && localport <= 65535)
        {
            if (setLocalPort(localport))
                while (1);
            WiFlyLocalPort = localport;
        }
        else
            errors++;
    }*/
    // List phone book entries
    else if (strstr(Modem_CommandBuffer, ("AT&PB?")) != NULL)
    {
        C64Println();
        for (int i = 0; i < ADDR_HOST_ENTRIES; i++)
        {
            C64Serial.print(i + 1);
            C64Print(F(":"));
            C64Println(readEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE)));
            yield();  // For 300 baud
        }
        C64Println();
        C64Print(F("Autostart: "));
        C64Serial.print(EEPROM.read(ADDR_HOST_AUTO));
        C64Println();
    }
    // Clear phone book
    else if (strstr(Modem_CommandBuffer, ("AT&PBCLEAR")) != NULL)
    {
        for (int i = 0; i < ADDR_HOST_ENTRIES; i++)
        {
            updateEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE), "\0");
        }
    }
    /*    else if (strstr(Modem_CommandBuffer, ("AT&PBCLEAR")) != NULL)
    {
    for (int i = 0; i < ADDR_HOST_ENTRIES - STATIC_PB_ENTRIES; i++)
    {
    updateEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE), "\0");
    }

    // To add static entries, update STATIC_PB_ENTRIES and add entries below increased x: ADDR_HOST_ENTRIES - x
    updateEEPROMPhoneBook(ADDR_HOSTS + ((ADDR_HOST_ENTRIES - 1) * ADDR_HOST_SIZE), F("WWW.COMMODORESERVER.COM:1541"));   // last entry
    updateEEPROMPhoneBook(ADDR_HOSTS + ((ADDR_HOST_ENTRIES - 2) * ADDR_HOST_SIZE), F("BBS.JAMMINGSIGNAL.COM:23"));       // second last entry

    updateEEPROMByte(ADDR_HOST_AUTO, 0);
    Modem_PrintOK();
    }*/
    // Add entry to phone book
    else if (strncmp(Modem_CommandBuffer, ("AT&PB"), 5) == 0)
    {
        char numString[2];
        numString[0] = Modem_CommandBuffer[5];
        numString[1] = '\0';

        int phoneBookNumber = atoi(numString);
        if (phoneBookNumber >= 1 && phoneBookNumber <= ADDR_HOST_ENTRIES && Modem_CommandBuffer[6] == '=')
        {
            updateEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE), Modem_CommandBuffer + 7);
        }
        else
            errors++;
    }

    else if (strncmp(Modem_CommandBuffer, ("AT"), 2) == 0)
    {
        for (int i = 2; i < strlen(Modem_CommandBuffer) && i < COMMAND_BUFFER_SIZE - 3;)
        {
            int WiFicounter = 0;
            int WiFiConnectSuccess = false;

            switch (Modem_CommandBuffer[i++])
            {
            case 'Z':   // ATZ
                Modem_LoadSavedSettings();
                //if (wifly.isSleeping())
                //    wake();
                break;

            case 'I':   // ATI
                ShowInfo(false);
                break;

                /*case 'I':
                switch (Modem_CommandBuffer[i++])
                {
                case '1':
                ShowInfo(false);
                break;

                default:
                i--;                        // User entered ATI
                case '0':
                C64Print(F("Commodore Wi-Fi Modem Hayes Emulation v"));
                C64Println(VERSION);
                break;
                }
                break;*/

            case 'A':   // ATA
                Modem_Answer();
                suppressOkError = true;
                break;

            case 'E':   // ATE
                switch (Modem_CommandBuffer[i++])
                {
                case '0':
                    Modem_EchoOn = false;
                    break;

                case '1':
                    Modem_EchoOn = true;
                    break;

                default:
                    errors++;
                }
                break;

            case 'H':       // ATH
                switch (Modem_CommandBuffer[i++])
                {
                case '0':
                    //if (wifly.isSleeping())
                    //    wake();
                    //else
                        Modem_Disconnect(false);
                    break;

                case '1':
                    //if (!wifly.isSleeping())
                    //    if (!wifly.sleep())
                    //        errors++;
                    break;

                default:
                    i--;                        // User entered ATH
                    //if (wifly.isSleeping())
                    //    wake();
                    //else
                        Modem_Disconnect(false);
                    break;
                }
                break;

            case 'O':
                if (Modem_isConnected)
                {
                    Modem_isCommandMode = false;
                }
                else
                    errors++;
                break;

            case 'Q':
                switch (Modem_CommandBuffer[i++])
                {
                case '0':
                    Modem_QuietMode = false;
                    break;

                case '1':
                    Modem_QuietMode = true;
                    break;

                default:
                    errors++;
                }
                break;

            case 'S':   // ATS
                switch (Modem_CommandBuffer[i++])
                {
                case '0':
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                        switch (Modem_CommandBuffer[i++])
                        {
                        case '0':
                            Modem_S0_AutoAnswer = 0;
                            break;

                        case '1':
                            Modem_S0_AutoAnswer = 1;
                            break;

                        default:
                            errors++;
                        }
                        break;
                    }
                    break;

                case '2':
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                        char numString[3] = "";

                        // Find index of last character for this setting.  Expects 1-3 numbers (ats2=43, ats2=126 etc)
                        int j = i;
                        for (int p = 0; (p < strlen(atCommandFirstChars)) && (j <= i + 2); p++)
                        {
                            if (strchr(atCommandFirstChars, Modem_CommandBuffer[j]))
                                break;
                            j++;
                        }

                        strncpy(numString, Modem_CommandBuffer + i, j - i);
                        numString[3] = '\0';

                        Modem_S2_EscapeCharacter = atoi(numString);

                        i = j;
                        break;
                    }
                    break;

                case '9':
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '9':
                        switch (Modem_CommandBuffer[i++])
                        {
                        case '=':
                            switch (Modem_CommandBuffer[i++])
                            {
                            case '0':
                                Modem_suppressErrors = 0;
                                break;

                            case '1':
                                Modem_suppressErrors = 1;
                                break;
                            }
                        }
                    }
                    break;
                }
                break;

            case 'V':   // ATV
                switch (Modem_CommandBuffer[i++])
                {
                case '0':
                    Modem_VerboseResponses = false;
                    break;

                case '1':
                    Modem_VerboseResponses = true;
                    break;

                default:
                    errors++;
                }
                break;


                /*
                X0 = 0-4
                X1 = 0-5, 10
                X2 = 0-6, 10
                X3 = 0-5, 7, 10
                X4 = 0-7, 10

                0 - OK
                1 - CONNECT
                2 - RING
                3 - NO CARRIER
                4 - ERROR
                5 - CONNECT 1200
                6 - NO DIALTONE
                7 - BUSY
                8 - NO ANSWER
                10 - CONNECT 2400
                11 - CONNECT 4800
                etc..
                */
            case 'X':   // ATX
                Modem_X_Result = (Modem_CommandBuffer[i++] - 48);
                if (Modem_X_Result < 0 || Modem_X_Result > 4)
                {
                    Modem_X_Result = 0;
                    errors++;
                }

                break;

            case '&':   // AT&
                switch (Modem_CommandBuffer[i++])
                {
                case 'C':
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '0':
                        Modem_DCDFollowsRemoteCarrier = false;
                        digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
                        break;

                    case '1':
                        Modem_DCDFollowsRemoteCarrier = true;
                        if (Modem_isConnected) {
                            digitalWrite(C64_DCD, Modem_ToggleCarrier(true));
                        }
                        else {
                            digitalWrite(C64_DCD, Modem_ToggleCarrier(false));
                        }
                        break;

                    default:
                        errors++;
                    }
                    break;

                case 'F':   // AT&F
                    Modem_LoadDefaults(false);
                    //if (wifly.isSleeping())
                    //    wake();

                    break;

                case 'K':   // AT&K
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '0':
                        Modem_flowControl = false;
                        break;

                    case '1':
                        Modem_flowControl = true;
                        break;

                    default:
                        errors++;
                    }
                    break;

                case 'R':   // AT&R = Reboot
                    C64Println(F("Restarting."));
                    C64Println();
                    ESP.restart();
                    while (1);
                    break;

                case 'M':   // AT&M
                    mode_Hayes = false;
                    updateEEPROMByte(ADDR_HAYES_MENU, mode_Hayes);
                    C64Println(F("Restarting in Menu mode."));
                    C64Println();
                    ESP.restart();
                    while (1);
                    break;

                case 'S':   // AT&S
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '0':
                        //Modem_dataSetReady = 0;
                        //break;
                    case '1':
                        //Modem_dataSetReady = 1;
                        //break;
                    case '2':
                        //Modem_dataSetReady = 2;
                        Modem_dataSetReady = Modem_CommandBuffer[i - 1] - '0x30';
#ifdef C64_DSR
                        Modem_DSR_Set();
#endif
                        break;

                    default:
                        errors++;
                    }
                    break;

                case 'W':   // AT&W
                    updateEEPROMByte(ADDR_MODEM_ECHO, Modem_EchoOn);
                    updateEEPROMByte(ADDR_MODEM_FLOW, Modem_flowControl);
                    updateEEPROMByte(ADDR_MODEM_VERBOSE, Modem_VerboseResponses);
                    updateEEPROMByte(ADDR_MODEM_QUIET, Modem_QuietMode);
                    updateEEPROMByte(ADDR_MODEM_S0_AUTOANS, Modem_S0_AutoAnswer);
                    updateEEPROMByte(ADDR_MODEM_S2_ESCAPE, Modem_S2_EscapeCharacter);
                    updateEEPROMByte(ADDR_MODEM_DCD, Modem_DCDFollowsRemoteCarrier);
                    updateEEPROMByte(ADDR_MODEM_X_RESULT, Modem_X_Result);
                    updateEEPROMByte(ADDR_MODEM_SUP_ERRORS, Modem_suppressErrors);
                    updateEEPROMByte(ADDR_MODEM_DSR, Modem_dataSetReady);
                    updateEEPROMByte(ADDR_BAUD, BAUD_RATE_FORCED);

                    //if (!(wifly.save()))
                    //    errors++;
                    break;

                    /*case '-':   // AT&-
                    C64Println();
                    C64Serial.print(freeRam());
                    C64Println();
                    break;*/
                }
                break;

            case '*':               // AT* Moving &ssid, &pass and &key to * costs 56 flash but saves 26 mimimum RAM.
                switch (Modem_CommandBuffer[i++])
                {
                case 'B':   // AT*B     Set baud rate
                    char newBaudRate;

                    newBaudRate = Modem_CommandBuffer[i++];

                    if (newBaudRate >= '0' && newBaudRate <= '3')
                    {
                        BAUD_RATE_FORCED = newBaudRate;
                    }
                    else
                        errors++;
                    break;

                case 'M':   // AT*M     Message sent to remote side when answering
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                    {
                        int j = 0;
                        for (; j < ADDR_ANSWER_MESSAGE_SIZE - 1; j++)
                        {
                            EEPROM.write(ADDR_ANSWER_MESSAGE + j, (Modem_LastCommandBuffer + i)[j]);
                            EEPROM.commit();
                        }

                        i = strlen(Modem_LastCommandBuffer);    // Consume the rest of the line.
                        break;
                    }

                    default:
                        errors++;
                    }
                    break;

                case 'S':   // AT*S     Set SSID
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                        WiFi.begin(Modem_LastCommandBuffer + i, SSID_passphrase.c_str());

                        WiFicounter = 0;
                        WiFiConnectSuccess = false;
                        while (WiFicounter < 40) {
                            delay(500);
                            Serial.print(".");
                            if (WiFi.status() == WL_CONNECTED) {
                                WiFiConnectSuccess = true;
                                break;
                            }
                            WiFicounter++;
                        }
                        if (WiFiConnectSuccess == false)
                            errors++;

                        //wifly.setSSID(Modem_LastCommandBuffer + i);

                        /*wifly.leave();
                        if (!wifly.join(20000))    // 20 second timeout
                            errors++;
                            */

                        i = strlen(Modem_LastCommandBuffer);    // Consume the rest of the line.
                        break;

                    default:
                        errors++;
                    }
                    break;
                    
                case 'P':   // AT*P     Set SSID passphrase
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                        SSID_passphrase = (String)(Modem_LastCommandBuffer + i);
                        //wifly.setPassphrase(Modem_LastCommandBuffer + i);

                        i = strlen(Modem_LastCommandBuffer);    // Consume the rest of the line.
                        break;

                    default:
                        errors++;
                    }
                    break;
                    /*
                case 'K':   // AT*K     Set SSID key for WEP
                    switch (Modem_CommandBuffer[i++])
                    {
                    case '=':
                        wifly.setKey(Modem_LastCommandBuffer + i);

                        i = strlen(Modem_LastCommandBuffer);    // Consume the rest of the line.
                        break;

                    default:
                        errors++;
                    }*/
                default:
                    errors++;
                }
                break;

                // Dialing should come last..
                // TODO:  Need to allow for spaces after D, DT, DP.  Currently fails.
            case 'D':   // ATD
                switch (Modem_CommandBuffer[i++])
                {
                case '\0':                          /* ATD = ATO.  Probably don't need this...' */
                    if (Modem_isConnected)
                    {
                        Modem_isCommandMode = false;
                    }
                    else
                        errors++;
                    break;

                case 'T':
                case 'P':
                    removeSpaces(&Modem_CommandBuffer[i]);

                    switch (Modem_CommandBuffer[i++])
                    {
                    case ',':       // ATD,
                    case '#':       // ATD#
                        // Phonebook dial
                        numString[0] = Modem_CommandBuffer[i];
                        numString[1] = '\0';

                        phoneBookNumber = atoi(numString);
                        if (phoneBookNumber >= 1 && phoneBookNumber <= ADDR_HOST_ENTRIES)
                        {
                            strncpy(address, readEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE)).c_str(), ADDR_HOST_SIZE);
                            removeSpaces(address);
                            Modem_Dialout(address);
                            suppressOkError = 1;
                        }
                        else
                            errors++;
                        break;

                    default:
                        i--;
                        Modem_Dialout(&Modem_CommandBuffer[i]);
                        suppressOkError = 1;
                        i = COMMAND_BUFFER_SIZE - 3;    // Make sure we don't try to process any more...
                        break;
                    }
                    break;

                case ',':       // ATD,
                case '#':       // ATD#
                    // Phonebook dial
                    removeSpaces(&Modem_CommandBuffer[i]);
                    numString[0] = Modem_CommandBuffer[i];
                    numString[1] = '\0';

                    phoneBookNumber = atoi(numString);
                    if (phoneBookNumber >= 1 && phoneBookNumber <= ADDR_HOST_ENTRIES)
                    {
                        strncpy(address, readEEPROMPhoneBook(ADDR_HOSTS + ((phoneBookNumber - 1) * ADDR_HOST_SIZE)).c_str(), ADDR_HOST_SIZE);
                        removeSpaces(address);
                        Modem_Dialout(address);
                        suppressOkError = 1;
                    }
                    else
                        errors++;
                    break;

                default:
                    i--;        // ATD
                    Modem_Dialout(&Modem_CommandBuffer[i]);
                    suppressOkError = 1;
                    i = COMMAND_BUFFER_SIZE - 3;    // Make sure we don't try to process any more...
                    break;
                }
                break;

            case '\n':
            case '\r':
                break;

            default:
                errors++;
            }
        }
    }

    if (!suppressOkError)           // Don't print anything if we just dialed out etc
    {
        if (Modem_suppressErrors || !errors)        // ats99=1 to disable errors and always print OK
            Modem_PrintOK();
        else if (errors)
            Modem_PrintERROR();
    }

    Modem_ResetCommandBuffer();
}

void Modem_Ring()
{
    Modem_isRinging = true;

    Modem_PrintResponse(2, ("\r\nRING"));
    if (Modem_S0_AutoAnswer != 0)
    {
#ifdef C64_RI
        digitalWrite(C64_RI, HIGH);
        delay(250);
        digitalWrite(C64_RI, LOW);
#endif
        Modem_Answer();
    }
    else
    {
#ifdef C64_RI
        digitalWrite(C64_RI, HIGH);
        delay(250);
        digitalWrite(C64_RI, LOW);
#endif
    }
}

void Modem_Connected(boolean incoming)
{
    if (Modem_X_Result == 0)
    {
        Modem_PrintResponse(1, ("CONNECT"));
    }
    else {
        switch (BAUD_RATE)
        {
        case 1200:
            Modem_PrintResponse(5, ("CONNECT 1200"));
            break;
        case 2400:
            Modem_PrintResponse(10, ("CONNECT 2400"));
            break;
        case 4800:
            Modem_PrintResponse(11, ("CONNECT 4800"));
            break;
        case 9600:
            Modem_PrintResponse(12, ("CONNECT 9600"));
            break;
        case 19200:
            Modem_PrintResponse(14, ("CONNECT 19200"));
            break;
        case 38400:
            Modem_PrintResponse(28, ("CONNECT 38400"));
            break;
        default:
            Modem_PrintResponse(1, ("CONNECT"));
        }
    }

    if (Modem_DCDFollowsRemoteCarrier)
        digitalWrite(C64_DCD, Modem_ToggleCarrier(true));

#ifdef C64_DSR    
    if (Modem_dataSetReady == 1)
        digitalWrite(C64_DSR, LOW);
#endif

    //if (!commodoreServer38k)
    //    CheckTelnet();
    isFirstChar = true;
    telnetBinaryMode = false;

    Modem_isConnected = true;
    Modem_isCommandMode = false;
    Modem_isRinging = false;

    if (incoming) {
        //wifly.println(F("CONNECTING..."));
        if (EEPROM.read(ADDR_ANSWER_MESSAGE))
        {
            for (int j = 0; j < ADDR_ANSWER_MESSAGE_SIZE - 1; j++)
            {
                // Assuming it was stored correctly with a trailing \0
                char temp = EEPROM.read(ADDR_ANSWER_MESSAGE + j);
                if (temp == '^')
                    wifly.print("\r\n");
                else
                    wifly.print(temp);
                if (temp == 0)
                    break;
            }
            wifly.println();
        }
    }
}

void Modem_ProcessData()
{
    while (C64Serial.available() >0)
    {
        if (commodoreServer38k)
        {
            //DoFlowControlC64ToModem();

            wifly.write(C64Serial.read());
        }
        else
        {
            // Command Mode -----------------------------------------------------------------------
            if (Modem_isCommandMode)
            {
                unsigned char unsignedInbound;
                //boolean petscii_char;

                if (Modem_flowControl)
                {
                    digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? LOW : HIGH));
                    //digitalWrite(C64_CTS, LOW);
                }
                //char inbound = toupper(_serial->read());
                char inbound = C64Serial.read();
                // C64 PETSCII terminal programs send lower case 0x41-0x5a, upper case as 0xc1-0xda
                /* Real modem:
                ASCII at = OK
                ASCII AT = OK
                PET at = ok
                PET AT = OK
                */
                /*if ((inbound >= 0xc1) && (inbound <= 0xda))
                petscii_char = true;
                inbound = charset_p_toascii_upper_only(inbound);

                if (inbound == 0)
                return;*/


                // Block non-ASCII/PETSCII characters
                unsignedInbound = (unsigned char)inbound;

                // Do not delete this.  Used for troubleshooting...
                //char temp[5];
                //sprintf(temp, "-%d-",unsignedInbound);
                //C64Serial.write(temp);

                if (unsignedInbound == 0x08 || unsignedInbound == 0x0a || unsignedInbound == 0x0d || unsignedInbound == 0x14) {}  // backspace, LF, CR, C= Delete
                else if (unsignedInbound <= 0x1f)
                    break;
                else if (unsignedInbound >= 0x80 && unsignedInbound <= 0xc0)
                    break;
                else if (unsignedInbound >= 0xdb)
                    break;

                if (Modem_EchoOn)
                {
                    if (!Modem_flowControl)
                        delay(100 / ((BAUD_RATE >= 2400 ? BAUD_RATE : 2400) / 2400));     // Slow down command mode to prevent garbage if flow control
                                                                                          // is disabled.  Doesn't work at 9600 but flow control should 
                                                                                          // be on at 9600 baud anyways.  TODO:  Fix
                    C64Serial.write(inbound);
                }

                if (IsBackSpace(inbound))
                {
                    if (strlen(Modem_CommandBuffer) > 0)
                    {
                        Modem_CommandBuffer[strlen(Modem_CommandBuffer) - 1] = '\0';
                    }
                }
                //else if (inbound != '\r' && inbound != '\n' && inbound != Modem_S2_EscapeCharacter)
                else if (inbound != '\r' && inbound != '\n')
                {
                    if (strlen(Modem_CommandBuffer) >= COMMAND_BUFFER_SIZE) {
                        //Display (F("CMD Buf Overflow"));
                        Modem_PrintERROR();
                        Modem_ResetCommandBuffer();
                    }
                    else {
                        // TODO:  Move to Modem_ProcessCommandBuffer?
                        if (Modem_AT_Detected)
                        {
                            Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                        }
                        else
                        {
                            switch (strlen(Modem_CommandBuffer))
                            {
                            case 0:
                                switch (unsignedInbound)
                                {
                                case 'A':
                                case 'a':
                                case 0xC1:
                                    Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                                    break;
                                }
                                break;
                            case 1:
                                switch (unsignedInbound)
                                {
                                case 'T':
                                case 't':
                                case '/':
                                case 0xD4:
                                    Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                                    Modem_AT_Detected = true;
                                    break;
                                }
                                break;
                            }
                        }

                        if (toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[0])) == 'A' && (Modem_CommandBuffer[1] == '/'))
                        {
                            strcpy(Modem_CommandBuffer, Modem_LastCommandBuffer);
                            if (Modem_flowControl)
                            {
                                digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));
                            }
                            Modem_ProcessCommandBuffer();
                            Modem_ResetCommandBuffer();  // To prevent A matching with A/ again
                        }
                    }
                }
                // It was a '\r' or '\n'
                else if (toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[0])) == 'A' && toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[1])) == 'T')
                {
                    if (Modem_flowControl)
                    {
                        digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));
                    }
                    Modem_ProcessCommandBuffer();
                }
                else
                {
                    Modem_ResetCommandBuffer();
                }
            }

            else    // Online ------------------------------------------
            {
                if (Modem_isConnected)
                {
                    char C64input = C64Serial.read();

                    // +++ escape
                    if (Modem_S2_EscapeCharacter < 128) // 128-255 disables escape sequence
                    {
                        if ((millis() - ESCAPE_GUARD_TIME) > escapeTimer)
                        {
                            if (C64input == Modem_S2_EscapeCharacter && lastC64input != Modem_S2_EscapeCharacter)
                            {
                                escapeCount = 1;
                                lastC64input = C64input;
                            }
                            else if (C64input == Modem_S2_EscapeCharacter && lastC64input == Modem_S2_EscapeCharacter)
                            {
                                escapeCount++;
                                lastC64input = C64input;
                            }
                            else
                            {
                                escapeCount = 0;
                                escapeTimer = millis();   // Last time non + data was read
                            }
                        }
                        else
                        {
                            escapeTimer = millis();   // Last time data was read
                        }


                        if (escapeCount == 3) {
                            Display("Escape!", true, 0);
                            escapeReceived = true;
                            escapeCount = 0;
                            escapeTimer = 0;
                            Modem_isCommandMode = true;
                            C64Println();
                            Modem_PrintOK();
                        }
                    }

                    lastC64input = C64input;

                    DoFlowControlC64ToModem();

                    // If we are in telnet binary mode, write and extra 255 byte to escape NVT
                    if ((unsigned char)C64input == NVT_IAC && telnetBinaryMode)
                        wifly.write(NVT_IAC);

                    int result = wifly.write((int16_t)C64input);
                }
            }
        }
    }
    if (Modem_flowControl && !commodoreServer38k)
    {
        digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));
    }
}

void Modem_Answer()
{
    if (!Modem_isRinging)    // If not actually ringing...
    {
        Modem_Disconnect(true);  // This prints "NO CARRIER"
        return;
    }

    Modem_Connected(true);
}

// Main processing loop for the virtual modem.  Needs refactoring!
void Modem_Loop()
{
    // Modem to C64 flow
    boolean wiflyIsConnected = wifly.connected();

    if (wiflyIsConnected && commodoreServer38k)
    {
        while (wifly.available() > 0)
        {
            yield();
            DoFlowControlModemToC64();
            C64Serial.write(wifly.read());
            delay(10);       // Microview: Slow things down a bit (delay 4).
                            // ESP8266: Slow things down a bit (delay 8).
        }
    }
    else
    {
        if (wiflyIsConnected) {
            // Check for new remote connection
            if (!Modem_isConnected && !Modem_isRinging)
            {
                //wifly.println(F("CONNECTING..."));

                Display(("INCOMING\nCALL"), true, 0);
                Modem_Ring();
                return;
            }

            // If connected, handle incoming data  
            if (Modem_isConnected)
            {
                // Echo an error back to remote terminal if in command mode.
                if (Modem_isCommandMode && wifly.available() > 0)
                {
                    // If we print this, remote end gets flooded with this message 
                    // if we go to command mode on the C64 and remote side sends something..
                    //wifly.println(F("error: remote modem is in command mode."));
                }
                else
                {
                    int data;

                    // Buffer for 1200 baud
                    char buffer[10];
                    int buffer_index = 0;

                    {
                        while (wifly.available() > 0)
                        {
                            yield();
                            int data = wifly.read();

                            // If first character back from remote side is NVT_IAC, we have a telnet connection.
                            if (isFirstChar) {
                                if (data == NVT_IAC)
                                {
                                    isTelnet = true;
                                }
                                else
                                {
                                    isTelnet = false;
                                }
                            }
                            
                            if (data == NVT_IAC && isTelnet)
                            {
                                {
                                    if (CheckTelnetInline())
                                        C64Serial.write(NVT_IAC);
                                }

                            }
                            else
                            {
                                {
                                    DoFlowControlModemToC64();
                                    if (BAUD_RATE >= 9600)
                                        delay(2);       // Microview: Slow things down a bit..  1 seems to work with Desterm 3.02 at 9600.
                                                        // ESP8266:  2 works in Novaterm, 1 does not.
                                    C64Serial.write(data);
                                }
                            }                            
                        }
                    }
                }
            }
        }
        else  // !wiflyIsConnected
        {
            // Check for a dropped remote connection while ringing
            if (Modem_isRinging)
            {
                Modem_Disconnect(true);
                return;
            }

            // Check for a dropped remote connection while connected
            if (Modem_isConnected)
            {
                Modem_Disconnect(true);
                return;
            }
        }
    }

    // C64 to Modem flow
    //Modem_ProcessData();
    while (C64Serial.available() > 0)
    {
        yield();
        if (commodoreServer38k)
        {
            DoFlowControlC64ToModem();

            wifly.write(C64Serial.read());
        }
        else
        {
            // Command Mode -----------------------------------------------------------------------
            if (Modem_isCommandMode)
            {
                unsigned char unsignedInbound;
                //boolean petscii_char;

                if (Modem_flowControl)
                {
                    // Tell the C64 to stop
                    digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? LOW : HIGH));       // Stop
                }
                //char inbound = toupper(_serial->read());
                char inbound = C64Serial.read();
                // C64 PETSCII terminal programs send lower case 0x41-0x5a, upper case as 0xc1-0xda
                /* Real modem:
                ASCII at = OK
                ASCII AT = OK
                PET at = ok
                PET AT = OK
                */
                /*if ((inbound >= 0xc1) && (inbound <= 0xda))
                petscii_char = true;
                inbound = charset_p_toascii_upper_only(inbound);

                if (inbound == 0)
                return;*/


                // Block non-ASCII/PETSCII characters
                unsignedInbound = (unsigned char)inbound;

                // Do not delete this.  Used for troubleshooting...
                //char temp[5];
                //sprintf(temp, "-%d-",unsignedInbound);
                //C64Serial.write(temp);

                if (unsignedInbound == 0x08 || unsignedInbound == 0x0a || unsignedInbound == 0x0d || unsignedInbound == 0x14) {}  // backspace, LF, CR, C= Delete
                else if (unsignedInbound <= 0x1f)
                    break;
                else if (unsignedInbound >= 0x80 && unsignedInbound <= 0xc0)
                    break;
                else if (unsignedInbound >= 0xdb)
                    break;

                if (Modem_EchoOn)
                {
                    if (!Modem_flowControl)
                        delay(100 / ((BAUD_RATE >= 2400 ? BAUD_RATE : 2400) / 2400));     // Slow down command mode to prevent garbage if flow control
                                                                                            // is disabled.  Doesn't work at 9600 but flow control should 
                                                                                            // be on at 9600 baud anyways.  TODO:  Fix
                    C64Serial.write(inbound);
                }

                if (IsBackSpace(inbound))
                {
                    if (strlen(Modem_CommandBuffer) > 0)
                    {
                        Modem_CommandBuffer[strlen(Modem_CommandBuffer) - 1] = '\0';
                    }
                }
                //else if (inbound != '\r' && inbound != '\n' && inbound != Modem_S2_EscapeCharacter)
                else if (inbound != '\r' && inbound != '\n')
                {
                    if (strlen(Modem_CommandBuffer) >= COMMAND_BUFFER_SIZE) {
                        //Display (F("CMD Buf Overflow"));
                        Modem_PrintERROR();
                        Modem_ResetCommandBuffer();
                    }
                    else {
                        // TODO:  Move to Modem_ProcessCommandBuffer?
                        if (Modem_AT_Detected)
                        {
                            Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                        }
                        else
                        {
                            switch (strlen(Modem_CommandBuffer))
                            {
                            case 0:
                                switch (unsignedInbound)
                                {
                                case 'A':
                                case 'a':
                                case 0xC1:
                                    Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                                    break;
                                }
                                break;
                            case 1:
                                switch (unsignedInbound)
                                {
                                case 'T':
                                case 't':
                                case '/':
                                case 0xD4:
                                    Modem_CommandBuffer[strlen(Modem_CommandBuffer)] = inbound;
                                    Modem_AT_Detected = true;
                                    break;
                                }
                                break;
                            }
                        }

                        if (toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[0])) == 'A' && (Modem_CommandBuffer[1] == '/'))
                        {
                            strcpy(Modem_CommandBuffer, Modem_LastCommandBuffer);
                            if (Modem_flowControl)
                            {
                                digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));       // Go
                            }
                            Modem_ProcessCommandBuffer();
                            Modem_ResetCommandBuffer();  // To prevent A matching with A/ again
                        }
                    }
                }
                // It was a '\r' or '\n'
                else if (toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[0])) == 'A' && toupper(charset_p_toascii_upper_only(Modem_CommandBuffer[1])) == 'T')
                {
                    if (Modem_flowControl)
                    {
                        digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));       // Go
                    }
                    Modem_ProcessCommandBuffer();
                }
                else
                {
                    Modem_ResetCommandBuffer();
                }
            }

            else    // Online ------------------------------------------
            {
                if (Modem_isConnected)
                {
                    yield();
                    char C64input = C64Serial.read();

                    // +++ escape
                    if (Modem_S2_EscapeCharacter < 128) // 128-255 disables escape sequence
                    {
                        if ((millis() - ESCAPE_GUARD_TIME) > escapeTimer)
                        {
                            if (C64input == Modem_S2_EscapeCharacter && lastC64input != Modem_S2_EscapeCharacter)
                            {
                                escapeCount = 1;
                                lastC64input = C64input;
                            }
                            else if (C64input == Modem_S2_EscapeCharacter && lastC64input == Modem_S2_EscapeCharacter)
                            {
                                escapeCount++;
                                lastC64input = C64input;
                            }
                            else
                            {
                                escapeCount = 0;
                                escapeTimer = millis();   // Last time non + data was read
                            }
                        }
                        else
                        {
                            escapeTimer = millis();   // Last time data was read
                        }


                        if (escapeCount == 3) {
                            Display("Escape!", true, 0);
                            escapeReceived = true;
                            escapeCount = 0;
                            escapeTimer = 0;
                            Modem_isCommandMode = true;
                            C64Println();
                            Modem_PrintOK();
                        }
                    }

                    lastC64input = C64input;

                    DoFlowControlC64ToModem();

                    // If we are in telnet binary mode, write and extra 255 byte to escape NVT
                    if ((unsigned char)C64input == NVT_IAC && telnetBinaryMode)
                        wifly.write(NVT_IAC);

                    int result = wifly.write((int16_t)C64input);
                }
            }
        }
    }
    if (Modem_flowControl)
    {
        digitalWrite(C64_CTS, (Modem_isCtsRtsInverted ? HIGH : LOW));       // Go
    }

    //digitalWrite(DCE_RTS, HIGH);
}

//#endif // HAYES

/*
void setBaudWiFi(unsigned int newBaudRate) {
    WiFly_BAUD_RATE = newBaudRate;
    WifiSerial.flush();
    delay(2);
    wifly.setBaud(newBaudRate);     // Uses set uart instant so no save and reboot needed
    delay(1000);
    WifiSerial.end();
    WifiSerial.begin(newBaudRate);

    // After setBaud changes the baud rate it loses control
    // as it doesn't see the AOK and then doesn't exit command mode
    // properly.  We send this command to get things back in order..
    wifly.getPort();
}

void setBaudWiFi2(unsigned int newBaudRate) {
    WifiSerial.flush();
    delay(2);
    WifiSerial.end();
    WifiSerial.begin(newBaudRate);
}
*/

int freeRam()
{
    extern int __heap_start, *__brkval;
    int v;
    return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// detRate baud detection
long detRate(int recpin)  // function to return valid received baud rate
                          // Note that the serial monitor has no 600 baud option and 300 baud
                          // doesn't seem to work with version 22 hardware serial library
{
    auto start = millis();
    uint32_t timeout = 5000;
    unsigned long timer;
    timer = millis() + 10000;

    /*u8x8.setCursor(0, 5);
    u8x8.print(start);*/
    /*delay(1000);
    u8x8.setCursor(0, 6);
    u8x8.print(millis() + 10000);
    delay(10000);
    u8x8.setCursor(0, 7);
    u8x8.print("Go..");*/

    while (1)
    {
        yield();
        u8x8.setCursor(0, 6);
        //u8x8.print(millis());
        u8x8.print(int((timeout - (millis() - start)) / 1000));

        //if ((millis()+10000 - 5000) < timer) {
        if (millis() - start > timeout) {
            u8x8.setCursor(0, 6);
            u8x8.print(" ");
            u8x8.setCursor(0, 3);
            u8x8.print("Time's up!");
            u8x8.setCursor(0, 4);
            u8x8.print("Defaulting to:");
            u8x8.setCursor(0, 5);
            u8x8.print(" ");
            u8x8.print(BAUD_RATE);

            delay(2000);
            return (0);
        }
        if (digitalRead(recpin) == 0) {
            //u8x8.setCursor(0, 3);
            //u8x8.print("Key pressed");
            //delay(2000);
            break;
        }
    }
    long baud;
    long rate = pulseIn(recpin, LOW); // measure zero bit width from character. ASCII 'U' (01010101) provides the best results.
    
    if (rate < 12)
        baud = 115200;
    else if (rate < 20)
        baud = 57600;
    else if (rate < 29)
        baud = 38400;
    else if (rate < 40)
        baud = 28800;
    else if (rate < 60)
        baud = 19200;
    else if (rate < 80)
        baud = 14400;
    else if (rate < 150)
        baud = 9600;
    else if (rate < 300)
        baud = 4800;
    else if (rate < 600)
        baud = 2400;
    else if (rate < 1200)
        baud = 1200;
    else if (rate < 2400)
        baud = 600;
    else if (rate < 4800)
        baud = 300;
    else
        baud = 0;
    return baud;
}

void updateEEPROMByte(int address, byte value)
{
    if (EEPROM.read(address) != value)
    {
        EEPROM.write(address, value);
        EEPROM.commit();
    }
}

void updateEEPROMPhoneBook(int address, String host)
{
    int i = 0;
    for (; i < 38; i++)
    {
        EEPROM.write(address + i, host.c_str()[i]);
    }
    EEPROM.commit();
}

String readEEPROMPhoneBook(int address)
{
    char host[ADDR_HOST_SIZE - 2];
    int i = 0;
    for (; i < ADDR_HOST_SIZE - 2; i++)
    {
        host[i] = EEPROM.read(address + i);
    }
    return host;
}


void processC64Inbound()
{
    char C64input = C64Serial.read();

    yield();

    if ((millis() - ESCAPE_GUARD_TIME) > escapeTimer)
    {

        if (C64input == Modem_S2_EscapeCharacter && lastC64input != Modem_S2_EscapeCharacter)
        {
            escapeCount = 1;
            lastC64input = C64input;
        }
        else if (C64input == Modem_S2_EscapeCharacter && lastC64input == Modem_S2_EscapeCharacter)
        {
            escapeCount++;
            lastC64input = C64input;
        }
        else
        {
            escapeCount = 0;
            escapeTimer = millis();   // Last time non + data was read
        }
    }
    else
        escapeTimer = millis();   // Last time data was read

    if (escapeCount == 3) {
        Display(("Escape!"), true, 0);
        escapeReceived = true;
        escapeCount = 0;
        escapeTimer = 0;
    }

    // If we are in telnet binary mode, write and extra 255 byte to escape NVT
    if ((unsigned char)C64input == NVT_IAC && telnetBinaryMode)
        wifly.write(NVT_IAC);

    //DoFlowControlC64ToModem();
    wifly.write((uint8_t)C64input);
}


void removeSpaces(char *temp)
{
    char *p1 = temp;
    char *p2 = temp;

    while (*p2 != 0)
    {
        *p1 = *p2++;
        if (*p1 != ' ')
            p1++;
    }
    *p1 = 0;
}
/*
boolean setLocalPort(int localport)
{
    if (WiFlyLocalPort != localport)
    {
        wifly.setPort(localport);
        wifly.save();
        WiFlyLocalPort = localport;

        if (WiFly_BAUD_RATE != 2400) {
            C64Println(F("\n\rReboot MicroView & WiFi to set new port."));
            return true;
        }
        else {
            C64Println(F("\n\rRebooting WiFi..."));
            wifly.reboot();
            delay(5000);
            configureWiFly();
            return false;
        }
    }
    else
        return false;
}
*/
int Modem_ToggleCarrier(boolean isHigh)
{
    // We get a TRUE (1) if we want to activate DCD which is a logic LOW (0) normally.
    // So if not inverted, send !isHigh.  TODO:  Clean this up.
    if (Modem_isDcdInverted)
        return(isHigh);
    else
        return(!isHigh);
}

#ifdef HAYES
/* C64 CTS and RTS are inverted.  To allow data to flow from the C64,
set CTS on the C64 to HIGH.  Normal computer would be LOW.
Function not currently in use.  Tried similar function for RTS
but caused issues with CommodoreServer at 38,400.
*/
void C64_SetCTS(boolean value)
{
    if (Modem_isCtsRtsInverted)
        value = !value;
    digitalWrite(C64_CTS, value);
}
#endif

void DisplayPhoneBook() {
    for (int i = 0; i < ADDR_HOST_ENTRIES; i++)
    {
        C64Serial.print(i + 1);
        C64Print(F(":"));
        C64Println(readEEPROMPhoneBook(ADDR_HOSTS + (i * ADDR_HOST_SIZE)));
    }
    C64Println();
    C64Print(F("Autostart: "));
    C64Serial.print(EEPROM.read(ADDR_HOST_AUTO));
    C64Println();
}

void Modem_Dialout(char* host)
{
    char* index;
    uint16_t port = 23;
    String hostname = String(host);

    if (strlen(host) == 0)
    {
//#ifdef HAYES
        if (mode_Hayes)
            Modem_PrintERROR();
//#endif
        return;
    }

    if ((index = strstr(host, ":")) != NULL)
    {
        index[0] = '\0';
        hostname = String(host);
        port = atoi(index + 1);
    }

    lastHost = hostname;
    lastPort = port;

    Connect(hostname, port, false);
}

/*
void configureWiFly() {
    // Enable DNS caching, TCP retry, TCP_NODELAY, TCP connection status
    wifly.setIpFlags(16 | 4 | 2 | 1);           // 23  // Does this require a save?
    wifly.setIpProtocol(WIFLY_PROTOCOL_TCP);    // Does this require a save?  If so, remove and set once on WiFly and document.

}
*/

/*
void wake()
{
    setBaudWiFi2(2400);
    wifly.wake();
    //configureWiFly();
    //setBaudWiFi(WiFly_BAUD_RATE);
    //isSleeping = false;
}*/

#ifdef C64_DSR
void Modem_DSR_Set()
{
    if (Modem_dataSetReady != 2)
    {
        pinMode(C64_DSR, OUTPUT);
        switch (Modem_dataSetReady)
        {
        case 0:     // 0=Force DSR signal active
            pinMode(C64_DSR, OUTPUT);
            digitalWrite(C64_DSR, HIGH);
            break;
        case 1:
            // 1=DSR active according to the CCITT specification.
            // DSR will become active after answer tone has been detected 
            // and inactive after the carrier has been lost.
            pinMode(C64_DSR, OUTPUT);
            if (Modem_isConnected)
            {
                digitalWrite(C64_DSR, HIGH);
            }
            else
            {
                digitalWrite(C64_DSR, LOW);
            }
            break;
        }

    }
}
#endif


// #EOF - Leave this at the very end...

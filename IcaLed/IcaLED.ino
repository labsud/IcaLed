/*********************************************************************
IcaLED
This program have been targetted on NODEMCU ( http://www.nodemcu.com/ ) for ESP8266 Wifi processors.
It's purpose is to get ICAL XML file containing events, filter and sort them, and display them
on a brunch of 8X8 matricial led displays powered with max7219.

Developped for LABSud, Montpellier FABLAB do display current and next events.

Feel free to adapt it for you own usage.

**********************************************************************
The program :
* Connects to wifi network with defined credentials
* Get an IP adress
* Displays the given IP on SSD1306 and serial debug
* Get UTC time with NTP, then compute local hour taking care of Daylight Saving Time (DST)
* Update system hour with local time & Program NTP Sync each 2 hours
* now() function will give current local hour.
* Displays unix local time on debug serail

Then :
* Each hour (or during program launch):
  - Get ICAL feed & parse it to get events
  - Verify ical feed is complete.
  - Skip old events
  - Sort events by date
  - Store it as an array of evets (start date, end date, name)
  - Prepare a big string to display on the scroller (with text format of dates, event name, etc..)
* Each 2 hours, resync time using NTP
* Each hour : resync ICAL feed to get latest events.
* Permanently : display latest computed sting of events.

Libraries used
* SPI (builtin sur ESP8266)
* Wire/I2C (builtin sur ESP8266)
* Wifi ESP8266 (builtin sur ESP8266)
* Adafruit_SSD1306 . Adafruit library modified by me to add support for ESP8266 http://forum.labsud.org/post505.html . Should continue to work on arduino.
* Adafruit_GFX (pre requis pour Adafruit_SSD1306) egalement adapte support ESP8266 http://www.labsud.org/post505.html . Should continue to work on arduino.
* LedControlSPIESP8266, bas sur LedControlSPI et adapt pour ajouter le support de l'ESP8266
* Time : Took from  http://www.pjrc.com/teensy/td_libs_Time.html

Written by JP CIVADE pour LABSUD
Licence GPL V3
***********************************************************************/
//*********************************************************************/
// VERSION
//*********************************************************************/
#define VERSION "1.0c"

//*********************************************************************/
// For Wifi
//*********************************************************************/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
WiFiClient client;
const char* ssid     = "SSID";
const char* password = "PASSWPRD";

//*********************************************************************/
// For SSD1306
//*********************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


//*********************************************************************/
// For NTP
//*********************************************************************/
#include <Time.h>                       // See http://www.pjrc.com/teensy/td_libs_Time.html
IPAddress timeServer(129, 6, 15, 28);   // time.nist.gov NTP server
//IPAddress timeServer(132,246,11,231); // tic.nrc.ca  NTP server
//IPAddress timeServer(138,96,64,10);   // canon.inria.fr NTP server
const int NTP_PACKET_SIZE = 48;         // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];    // buffer to hold incoming and outgoing packets
WiFiUDP udp;
const int timeZone = +1;                // offset from UTC. +1 hour Central European Time.
unsigned int localPort = 8888;          // local port to listen for UDP packets
time_t prevDisplay = 0;                 // when the digital clock was displayed
#define TIMESYNCINTERVAL 7200           // 2 hours between two NTP accesses 

//*********************************************************************/
// For OLed display SSD1306
//*********************************************************************/
// Pinout :                                                           */
// D4 from NodeMCU Gpio2) = SCL                                       */
// D3 from NodeMCU Gpio0) = SDA                                       */
//*********************************************************************/
#define OLED_RESET 16 // unused since OLED hasen't got reset (4 pins)
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Hauteur incorrecte, merci de corriger Adafruit_SSD1306.h!");
#endif

//*********************************************************************/
// For max7219 8X8 LED matrix display
//*********************************************************************/
/* Pinout sur NodeMCU                                                 */
/* D5 = GPIO14 = Clk   is connected to CLK  [pin 13 on max7219]       */
/* D6 = GPIO12 = MISO  not connected                                  */
/* D7 = GPIO13 = MOSI  is connected to DATA in [pin 1 on max7219]     */
/* D8 = GPIO15 = SS    is connected to LOAD [pin 12 on max7219]       */
/* LED= GPIO16 =       for tests                                      */
//*********************************************************************/
#define NUMLEDDEVICES 8  // number of attached led devices
#define SCROLLSPEED 80   // Scroll Speed from 0 > 100 
#include "LedControlSPIESP8266.h" //  include the the library
#define LEDCSPIN  15     // Chip select pin for MAX7219
LedControl lc = LedControl(LEDCSPIN, NUMLEDDEVICES); //
int photoresistor;       // to store photoresistor ADC read value
int luminosity;          // Luminosity corrected with ambiant limunosity

// The character set courtesy of cosmicvoid.  It is rowwise
byte Font8x5[96 * 8] =
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // [space]
  0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02, 0x00, // !
  0x05, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, // "
  0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00, // #
  0x0E, 0x15, 0x05, 0x0E, 0x14, 0x15, 0x0E, 0x00, // $
  0x13, 0x13, 0x08, 0x04, 0x02, 0x19, 0x19, 0x00, // %
  0x06, 0x09, 0x05, 0x02, 0x15, 0x09, 0x16, 0x00, // &
  0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // '
  0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, // (
  0x01, 0x02, 0x04, 0x04, 0x04, 0x02, 0x01, 0x00, // )
  0x00, 0x0A, 0x15, 0x0E, 0x15, 0x0A, 0x00, 0x00, // *
  0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00, // +
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01, // ,
  0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, // -
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, // .
  0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x00, // /
  0x0E, 0x11, 0x19, 0x15, 0x13, 0x11, 0x0E, 0x00, // 0
  0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00, // 1
  0x0E, 0x11, 0x10, 0x0C, 0x02, 0x01, 0x1F, 0x00, // 2
  0x0E, 0x11, 0x10, 0x0C, 0x10, 0x11, 0x0E, 0x00, // 3
  0x08, 0x0C, 0x0A, 0x09, 0x1F, 0x08, 0x08, 0x00, // 4
  0x1F, 0x01, 0x01, 0x0E, 0x10, 0x11, 0x0E, 0x00, // 5
  0x0C, 0x02, 0x01, 0x0F, 0x11, 0x11, 0x0E, 0x00, // 6
  0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02, 0x00, // 7
  0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00, // 8
  0x0E, 0x11, 0x11, 0x1E, 0x10, 0x08, 0x06, 0x00, // 9
  0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, // :
  0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x02, 0x01, // ;
  0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00, // <
  0x00, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x00, 0x00, // =
  0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01, 0x00, // >
  0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04, 0x00, // ?
  0x0E, 0x11, 0x1D, 0x15, 0x0D, 0x01, 0x1E, 0x00, // @
  0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00, // A
  0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F, 0x00, // B
  0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E, 0x00, // C
  0x07, 0x09, 0x11, 0x11, 0x11, 0x09, 0x07, 0x00, // D
  0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F, 0x00, // E
  0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01, 0x00, // F
  0x0E, 0x11, 0x01, 0x0D, 0x11, 0x19, 0x16, 0x00, // G
  0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00, // H
  0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07, 0x00, // I
  0x1C, 0x08, 0x08, 0x08, 0x08, 0x09, 0x06, 0x00, // J
  0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11, 0x00, // K
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0F, 0x00, // L
  0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00, // M
  0x11, 0x13, 0x13, 0x15, 0x19, 0x19, 0x11, 0x00, // N
  0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, // O
  0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01, 0x00, // P
  0x0E, 0x11, 0x11, 0x11, 0x15, 0x09, 0x16, 0x00, // Q
  0x0F, 0x11, 0x11, 0x0F, 0x05, 0x09, 0x11, 0x00, // R
  0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E, 0x00, // S
  0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, // T
  0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, // U
  0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00, // V
  0x41, 0x41, 0x41, 0x49, 0x2A, 0x2A, 0x14, 0x00, // W
  0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00, // X
  0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x00, // Y
  0x1F, 0x10, 0x08, 0x04, 0x02, 0x01, 0x1F, 0x00, // Z
  0x07, 0x01, 0x01, 0x01, 0x01, 0x01, 0x07, 0x00, // [
  0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00, // \
  0x07, 0x04, 0x04, 0x04, 0x04, 0x04, 0x07, 0x00, // ]
  0x00, 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, // ^
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, // _
  0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, // `
  0x00, 0x00, 0x06, 0x08, 0x0E, 0x09, 0x0E, 0x00, // a
  0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D, 0x00, // b
  0x00, 0x00, 0x06, 0x09, 0x01, 0x09, 0x06, 0x00, // c
  0x10, 0x10, 0x16, 0x19, 0x11, 0x19, 0x16, 0x00, // d
  0x00, 0x00, 0x06, 0x09, 0x07, 0x01, 0x0E, 0x00, // e
  0x04, 0x0A, 0x02, 0x07, 0x02, 0x02, 0x02, 0x00, // f
  0x00, 0x00, 0x06, 0x09, 0x09, 0x06, 0x08, 0x07, // g
  0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x11, 0x00, // h
  0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, // i
  0x04, 0x00, 0x06, 0x04, 0x04, 0x04, 0x04, 0x03, // j
  0x01, 0x01, 0x09, 0x05, 0x03, 0x05, 0x09, 0x00, // k
  0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, // l
  0x00, 0x00, 0x15, 0x2B, 0x29, 0x29, 0x29, 0x00, // m
  0x00, 0x00, 0x0D, 0x13, 0x11, 0x11, 0x11, 0x00, // n
  0x00, 0x00, 0x06, 0x09, 0x09, 0x09, 0x06, 0x00, // o
  0x00, 0x00, 0x0D, 0x13, 0x13, 0x0D, 0x01, 0x01, // p
  0x00, 0x00, 0x16, 0x19, 0x19, 0x16, 0x10, 0x10, // q
  0x00, 0x00, 0x0D, 0x13, 0x01, 0x01, 0x01, 0x00, // r
  0x00, 0x00, 0x0E, 0x01, 0x06, 0x08, 0x07, 0x00, // s
  0x00, 0x02, 0x07, 0x02, 0x02, 0x02, 0x04, 0x00, // t
  0x00, 0x00, 0x11, 0x11, 0x11, 0x19, 0x16, 0x00, // u
  0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00, // v
  0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00, // w
  0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, // x
  0x00, 0x00, 0x09, 0x09, 0x09, 0x0E, 0x08, 0x06, // y
  0x00, 0x00, 0x0F, 0x08, 0x06, 0x01, 0x0F, 0x00, // z
  0x04, 0x02, 0x02, 0x01, 0x02, 0x02, 0x04, 0x00, // {
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, // |
  0x01, 0x02, 0x02, 0x04, 0x02, 0x02, 0x01, 0x00, // }
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ~
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // [DEL] = 7F
};
byte lentbl_S[96] =
{
  // width of chars
  2, 2, 3, 5, 5, 5, 5, 2,
  3, 3, 5, 5, 2, 5, 1, 5,
  5, 4, 5, 5, 5, 5, 5, 5,
  5, 5, 1, 2, 4, 4, 4, 5,
  5, 5, 5, 5, 5, 5, 5, 5,
  5, 3, 5, 5, 4, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 7,
  5, 5, 5, 3, 5, 3, 5, 5,
  2, 4, 5, 4, 5, 4, 4, 4,
  5, 2, 3, 4, 2, 6, 5, 4,
  5, 5, 5, 4, 3, 5, 5, 5,
  5, 4, 4, 3, 2, 3, 0, 0
};

//*********************************************************************/
// Calendar
//*********************************************************************/
const char* url  = "/my/calendar.ical";
const char* host = "www.mywebsite.org";
#define NBREVENTS 30       // Number of events stored in array
#define EVENTTEXTSIZE 80   // Size of text description of event    "'Initiation a la decoupe laser'
#define EVENTDATESIZE 50   // Size of date part of event + spaces. " le Vendredi 31 Septembre 18h15->20h00          "
#define HEADERSIZE 50      // Banner (text introduction)
#define HEADERTEXT "              *** Agenda MyWebsite ***               "
#define FOOTERSIZE 50
#define FOOTERTEXT "Informations et reservation : www.website.org        "
char allEvents[((EVENTTEXTSIZE + EVENTDATESIZE)*(NBREVENTS)) + HEADERSIZE + FOOTERSIZE ]; // string containing all processed events to display.
int allEventsSize = 0;     // number of chars if allEvents
time_t bEvent[NBREVENTS];  // array of NBREVENTS events: start date, end date, description.
time_t eEvent[NBREVENTS];
char  dEvent[NBREVENTS][EVENTTEXTSIZE];
int nbrEvents = 0;         // number of active events in array
const char* monthNames[] = { "Janvier", "Fevrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre", "Novembre", "Decembre"};
//const char* monthNames[] = { "January","February","March","April","May","June","Juillet","Aout","Septembre","Octobre","Novembre","Decembre"};

const char* dayNames[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};

//*********************************************************************/
// Vars
//*********************************************************************/
unsigned long lastConnectionTime = 0;                  // last time you connected to the server, in milliseconds
static const unsigned long connectionInterval = 60 * (60 * 1000); // delay between ICAL Feeds updates, in milliseconds. Here 1 hour.
// State which we do not wish to continuously allocate/deallocate.

//*********************************************************************/
// send an NTP request to the time server at the given address
//*********************************************************************/
void sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

//*********************************************************************/
// Get NTP Time
//*********************************************************************/
time_t getNtpTime() {
  for (int i = 0; i < 5; i++) {
    while (udp.parsePacket() > 0) ; // discard any previously received packets
    Serial.println("Transmit NTP Request");
    sendNTPpacket(timeServer);
    delay(2500);
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      unsigned long epoch = secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
      epoch = epoch + dstOffset(epoch);  //Adjust for Daylight Saving Time
      return epoch;
    }
    Serial.println("No NTP Response.");
  }
  Serial.println("Could not setup time :-(");
  return 0; // return 0 if unable to get the time
}

//*********************************************************************/
// Compute Daylight Sayving time Offet from passed UnixTime.
// Returns 0 (winter) or 3600s (1h, summer ) depending or currrent date
//*********************************************************************/
int dstOffset (unsigned long unixTime)
{
  //Receives unix epoch time and returns seconds of offset for local DST
  //Valid thru 2099 for France only, Calculations from "http://www.webexhibits.org/daylightsaving/i.html"
  //Code idea from jm_wsb @ "http://forum.arduino.cc/index.php/topic,40286.0.html"
  //Get epoch times @ "http://www.epochconverter.com/" for testing
  //DST update wont be reflected until the next time sync
  time_t t = unixTime;
  // USA DST
  //  int beginDSTDay = (14 - (1 + year(t) * 5 / 4) % 7);
  //  int beginDSTMonth=3;
  //  int endDSTDay = (7 - (1 + year(t) * 5 / 4) % 7);
  //  int endDSTMonth=11;
  // European DST
  int beginDSTDay = (31 - (5 * year(t) / 4 + 4) % 7);
  int beginDSTMonth = 3;
  int endDSTDay = (31 - (5 * year(t) / 4 + 4) % 7);
  int endDSTMonth = 10;
  if (((month(t) > beginDSTMonth) && (month(t) < endDSTMonth))
      || ((month(t) == beginDSTMonth) && (day(t) > beginDSTDay))
      || ((month(t) == beginDSTMonth) && (day(t) == beginDSTDay) && (hour(t) >= 2))
      || ((month(t) == endDSTMonth) && (day(t) < endDSTDay))
      || ((month(t) == endDSTMonth) && (day(t) == endDSTDay) && (hour(t) < 1)))
    return (3600);  //Add back in one hours worth of seconds - DST in effect
  else
    return (0);  //NonDST
}

//*********************************************************************/
// DisplayEventsOnSSD1306
// There's no usable scoll on SSD1306 and o way to scoll properly
// fonts are 5X7 + 1 pixel
// So print a windows of 20 chars of the text @x,const, and move x from 5+1 to zero
// then move the window right 1 char
// It's not so smooth as a real scroll, but it works.
//*********************************************************************/
void DisplayEventsOnSSD1306() {
  int displaylen = 20;            // displayable window length
  char smallstr[displaylen];      // temp string for extracting a window of displaylen chars from allEvents
  int font = 5 + 1;               // font size (size 1)

  display.setTextSize(1);

  for (int z = 0; z < (allEventsSize - displaylen); z++) {
    strncpy (smallstr, &allEvents[z], displaylen);
    smallstr[displaylen] = 0;
    for (int pixel = (font); pixel > 0; pixel--) {
      display.clearDisplay();
      display.setCursor(pixel, 26);
      display.setTextColor(WHITE);
      display.print(smallstr);
      display.display();
      delay(1); // HACK!!! for keeping alive watchdog.
    }
  }
}

//*********************************************************************/
// DisplayEventsOnLed
//*********************************************************************/
void DisplayEventsOnLed() {
  int i, j, k;
  int curcharix = 0;       // Current char index on string
  int curcharbit = 0;      // Current bit processed
  int curcharixsave = 0;
  int curcharbitsave = 0;
  int curcharixsave2 = 0;
  int curcharbitsave2 = 0;
  char curchar;            // Current char processed

  while (curcharix != -1) {
    // Analog read photoresistor
    // photoresistor=analogRead(A0);

    // Sets brightness (0~15 possible values)
    //luminosity = map(photoresistor, 1000, 200, 0, 15);
    luminosity = 8;

    for (int i = 0; i < NUMLEDDEVICES; i++) {
      // The MAX72XX is in power-saving mode on startup
      lc.shutdown(i, false);
      // Set the brightness to a medium values
      lc.setIntensity(i, luminosity);
      // and clear the display
      lc.clearDisplay(i);
    }

    curcharixsave2 = curcharix;
    curcharbitsave2 = curcharbit;

    // Loop through our 8 displays
    // depending of led matrix components, one of the following must be uncommented
    // if chars are swapped, use the other one....
//    for (i = NUMLEDDEVICES-1; i >=0; i--) {
    for (i = 0; i <= NUMLEDDEVICES; i++) {
  
      // Set up rows on current  display
      for (j = 0; j < 8; j++) {
        byte outputbyte = 0;
        curchar = allEvents[curcharix];
        curcharixsave = curcharix;
        curcharbitsave = curcharbit;

        // Copy over data for 8 columns to current row and send it to current display
        for (k = 7; k >= 0; k--)  {
          // This byte is the bitmap of the current character for the current row
          byte currentcharbits = Font8x5[((curchar - 32) * 8) + j];
          if (currentcharbits & (1 << curcharbit)) {
            outputbyte |= (1 << k);
          }
          // advance the current character bit of current character
          curcharbit ++;

          // we are past the end of this character, so advance.
          if (curcharbit > lentbl_S[curchar - 32]) {
            curcharbit = 0;
            curcharix += 1;
            if (curcharix + 1 > allEventsSize) {
              curcharix = 0;
            }
            curchar = allEvents[curcharix];
          } // end we are past the end of this character, so advance.
        } // end Copy over data for 8 columns

        lc.setRow(i, j, outputbyte);

        // if this is not the last row, roll back advancement, if it is, leave the counters advanced.
        if (j != 7) {
          curcharix = curcharixsave;
          curcharbit = curcharbitsave;
        } // end if this is not the last row
      } // end Set up rows on current  display
    } // end Loop through our 8 displays

    curcharix = curcharixsave2;
    curcharbit = curcharbitsave2;
    curchar = allEvents[curcharix];

    // advance the current character bit of current character
    curcharbit ++;

    if (curcharbit > lentbl_S[curchar - 32]) // we are past the end of this character, so advance.
    {
      curcharbit = 0;
      curcharix += 1;
      if (curcharix + 1 > allEventsSize) {
        curcharix = -1;
      }
      curchar = allEvents[curcharix];
    }
    delay(101 - SCROLLSPEED);
  }

}

//*********************************************************************/
// Connect or reconnect wifi
//*********************************************************************/
void connectWifi() {
  // Trying to (re)connect to wifi network
  Serial.println("Connecting wifi...");
  display.println("Connecting wifi...");
  display.display();
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  // Connected ! Display SSID & IP
  Serial.println("Connected.");
  display.println("Connected.");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
}

//*********************************************************************/
// Digital clock display:
// Display time on SDSD1306
//*********************************************************************/
void digitalClockDisplay() {
  // digital clock display of the time
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(4, 0);
  printDigits (day(), 0);
  printDigits (month(), ' ');
  printDigits(year(), ' ');

  display.setTextSize(2);
  display.setCursor(14, 25);
  // digital clock display of the time
  display.print(hour());
  printDigits(minute(), ':');
  printDigits(second(), ':');

  // refresh display
  display.display();
}

//*********************************************************************/
// Debug: Displays passsed time on serial
//*********************************************************************/
void digitalClockDisplay(time_t t) {
  // digital clock display of the time
  Serial.print(t);
  Serial.print(" = ");
  Serial.print(hour(t));
  sprintDigits(minute(t), ':');
  sprintDigits(second(t), ':');
  Serial.print(" ");
  Serial.print(day(t));
  Serial.print("/");
  Serial.print(month(t));
  Serial.print("/");
  Serial.print(year(t));
  Serial.println();
}

//*********************************************************************/
// Utility for digital clock display:
// prints preceding separator and leading 0
// separator can be set tu NULL for no separator.
//*********************************************************************/
void printDigits(int digits, char separator) {
  if (separator)
    display.print(separator);
  if (digits < 10)
    display.print('0');
  display.print(digits);
}

//*********************************************************************/
// Same for doing it onto a string...
//*********************************************************************/
void sprintDigits(int digits, char separator) {
  if (separator)
    Serial.print(separator);
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

//*********************************************************************/
// this method makes a HTTP connection to the server:
//*********************************************************************/
boolean httpRequest() {
  Serial.println("Connecting to host");

  // if there's a successful connection:
  if (client.connect(host, 80)) {
    Serial.println("Requesting URL");
    Serial.print(host);
    Serial.println(url);

    // This will send the request to the server
    client.print(String("GET http://") + host + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: ESP8266-Scroller\r\n" +
                 "Connection: close\r\n\r\n");
    for (int i = 0; i < 5; i++) {
      if (!client.available()) {
        delay(100);
      }
      else {
        break;
      }
    }

    // note the time that the connection was made:
    lastConnectionTime = millis();

    return true;
  } else {
    // if you couldn't make a connection:
    Serial.println("Connection failed");
    client.stop();

    return false;
  }
}

//*********************************************************************/
// Replace unicoded accentuated char with unaccentuated version.
//*********************************************************************/
void uniDecode(char *str) {
  int j = 0;
  char tempstr[EVENTTEXTSIZE];
  // initialize string
  memset (tempstr, 0, EVENTTEXTSIZE);

  for (int i = 0; i < strlen(str); i++) {
    if (str[i] == 0xC3) { // unicode prefix?
      i += 1; // get next char since unicaode is 2 bytes encoding
      // a variants
      if ( (str[i] >= 0xA0) && (str[i] <= 0xA5)) {
        tempstr[j++] = 'a';
      }
      // e variants
      if ( (str[i] >= 0xA8) && (str[i] <= 0xAB)) {
        tempstr[j++] = 'e';
      }
      // i variants
      if ( (str[i] >= 0xAC) && (str[i] <= 0xAF)) {
        tempstr[j++] = 'i';
      }
      // o variants
      if ( (str[i] >= 0xB3) && (str[i] <= 0xB6)) {
        tempstr[j++] = 'o';
      }
      // u variants
      if ( (str[i] >= 0xB9) && (str[i] <= 0xBC)) {
        tempstr[j++] = 'u';
      }
      // ae
      if (str[i] == 0xA6) {
        tempstr[j++] = 'a';
        tempstr[j++] = 'e';
      }
      // c cedilla
      if (str[i] == 0xA7) {
        tempstr[j++] = 'c';
      }
    }
    else {
      tempstr[j++] = str[i];
    }
  }
  // Overwrite passed string with decoded content
  strcpy (str, tempstr);
  str[j] = 0;
}

//*********************************************************************/
// Swap sort events
//*********************************************************************/
void sortEvents() {
  time_t tmpbEvent;
  time_t tmpeEvent;
  char  tmpdEvent[EVENTTEXTSIZE];

  for (int j = 0; j < nbrEvents - 1; j++) {
    for (int i = j; i >= 0; i--) {
      if (bEvent[i] > bEvent[i + 1]) {
        // t=tab[i];
        tmpbEvent = bEvent[i];
        tmpeEvent = eEvent[i];
        memset (tmpdEvent, 0, EVENTTEXTSIZE);
        strcpy(tmpdEvent, dEvent[i]);
        // tab[i]=tab[i+1];
        bEvent[i] = bEvent[i + 1];
        eEvent[i] = eEvent[i + 1];
        memset (dEvent[i], 0, EVENTTEXTSIZE);
        strcpy(dEvent[i], dEvent[i + 1]);
        //tab[i+1]=t;
        bEvent[i + 1] = tmpbEvent;
        eEvent[i + 1] = tmpeEvent;
        memset (dEvent[i + 1], 0, EVENTTEXTSIZE);
        strcpy(dEvent[i + 1], tmpdEvent);
      }
    }
  }
}

//*********************************************************************/
// Parse an Ical file
//*********************************************************************/
// FORMAT :
// BEGIN:VCALENDAR
//  BEGIN:VEVENT
//    DTSTART (ex: DTSTART;TZID=Europe/Paris:20150818T181500 )
//    DTEND (ex: DTEND;TZID=Europe/Paris:20150818T200000 )
//    SUMMARY (ex: SUMMARY:Utilisation des imprimantes 3D du lab )
//  END:VEVENT
// END:VCALENDAR
//*********************************************************************/
void parseResponse() {
  String s_ddate;                 // for storing date temporarly in String format
  String s_event;                 // for storing event description temporarly
  tmElements_t tm_date;           // for storing date temporarly in tmElements_t format
  char oneEvent[EVENTTEXTSIZE + 30]; // String for storing one decoded event, ready to aggregate with allEvents
  bool eventStarted = false;  // state machine flag for decoding events.
  int i = 0;                  // loop counter
  bool endOfFeedRead = false; // State flag to be sure feed have beet completely read=.

  while (client.available()) {
    // if any supported tokens
    String line = client.readStringUntil('\n');
    //Serial.println(line);
    delay(10); // for 4G
    if (line.startsWith("END:VCALENDAR")) {
      endOfFeedRead = true;
    }
    if (line.startsWith("BEGIN:VEVENT")) {
      eventStarted = true;
    }
    if  (line.startsWith("END:VEVENT")) {
      eventStarted = false;
      if (i <20) {
        i += 1;
        }
      if (i == 20) {
        Serial.print ("Warning : Ical feed contains more than ");
        Serial.print (NBREVENTS);
        Serial.println (" events.");
      }
    }
    if  (eventStarted && (i < NBREVENTS) ) { // We're only interrested on DTSTART/END for events.
      if ((line.startsWith("DTSTART")) || (line.startsWith("DTEND"))) {
        // skip timezone
        s_ddate = line.substring(line.indexOf(':') + 1);
        // convert to Y/M/D in a tmElements_t struct
        tm_date.Year =  CalendarYrToTm(s_ddate.substring(0, 4).toInt()); // year (since 1970)
        tm_date.Month = s_ddate.substring(4, 6).toInt(); // month
        tm_date.Day =   s_ddate.substring(6, 8).toInt(); // day
        tm_date.Hour =  s_ddate.substring(9, 11).toInt(); // hour
        tm_date.Minute = s_ddate.substring(11, 13).toInt(); // min
        tm_date.Second = s_ddate.substring(13, 15).toInt(); // sec
        // Convert in time_t format ans store it
        if (line.startsWith("DTSTART")) {
          bEvent[i] = makeTime(tm_date);
        }
        else {
          eEvent[i] = makeTime(tm_date);
        }
        // hack : if endevent <= now(), event is past. Discard it.
        if (eEvent[i] <= now()) {
          eventStarted = false;
        }
      }
      if  (line.startsWith("SUMMARY")) {
        // Event description
        s_event = line.substring(line.indexOf(':') + 1);
        s_event.toCharArray(dEvent[i], s_event.length());
        // Unicode decode
        uniDecode (dEvent[i]);
      }
    }
  } // end while client available

  // ICAL file completely read?
  if (endOfFeedRead == true) {
    // Debug display # of valid events
    Serial.print("Valid events read: ");
    Serial.println(i);
    display.print("Valid events read: ");
    display.println(i);
    // Store number of valid events read.
    nbrEvents = i;

    // Sort events
    Serial.println("Sorting...");
    display.println("Sorting...");
    sortEvents();

    Serial.println("Generate big string...");
    display.println("Generate big string...");
    Serial.print(nbrEvents);
    Serial.println(" Events");
    // And generate a big string to display on Led matrix...
    memset (allEvents, '\0', sizeof(allEvents));
    strcpy (allEvents, HEADERTEXT);
    // Concatenate all events
    for (i = 0; i < nbrEvents; i++) {
      // If event is now
      if ( (bEvent[i] > now()) && (eEvent[i] < now()) ) {
        sprintf (oneEvent, "'%s' [En ce moment]            ", dEvent[i]);
        strcat (allEvents, oneEvent);
      }
      else {
        // Asssemble data for one event
        sprintf (oneEvent, "'%s' le %s %d %s %02dh%02d->%02dh%02d            ",
                 dEvent[i],
                 dayNames[weekday(bEvent[i]) - 1],
                 day(bEvent[i]),
                 monthNames[month(bEvent[i]) - 1],
                 hour(bEvent[i]),
                 minute(bEvent[i]),
                 hour(eEvent[i]),
                 minute(eEvent[i]));
        // and concat to all evets string
        strcat (allEvents, oneEvent);
      }
    }
    // actualize string length
    strcat (allEvents, FOOTERTEXT);
    allEventsSize = strlen(allEvents);
    // And display them
    Serial.println(allEvents);
    Serial.println("");
  }
  else {
    nbrEvents = 0;
    Serial.println ("ERROR: Ical feed uncomplete (no 'END:VCALENDAR' markup).");
  }
}

//*********************************************************************/
// Read feeds & parse them, eventually reconnecting to wifi before
//*********************************************************************/
void GetFeeds() {
  // see if connected to network , and reconnect if necessary
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  // call now() because it's trigs NTP resync. Resync have to be done before
  // getting ical feeds since it cannot occurs during parsing... (http already in progress  )
  now();

  // get Ical feed and parse it
  if (httpRequest()) {
    Serial.println("Parsing...");
    parseResponse();
    Serial.println("Response parsed");
    // Flush all remaining data
    while (client.connected()) {
      while (client.available()) {
        client.read();
      }
    }
    client.stop();
  }
}


//*********************************************************************/
// Arduino Setup Function
//*********************************************************************/
void setup() {

  // Serial is only for debug
  Serial.begin(115200);
  Serial.println(""); // to skip garbage
  Serial.println("Init Oled Display");
  // initialize with the I2C addr 0x3C (for the 128x64)

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C, 0);
  // Init displays Labsud Logo for 500 ms
  display.display();
  delay(500);

  // Display banner
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("IcaLed ");
  display.setTextSize(1);
  display.println(VERSION);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);

  // Init 8X8 leds
  for (int i = 0; i < NUMLEDDEVICES; i++) {
    lc.shutdown(i, false); // turn off power saving, enables display
    lc.setIntensity(i, 8); // sets brightness (0~15 possible values)
    lc.clearDisplay(i);    // clear screen
  }
  // Connecting Wifi
  connectWifi();

  // Now get current time from Internet
  display.println("Getting NTP time...");
  display.display();
  udp.begin(localPort);
  setSyncProvider(getNtpTime);
  setSyncInterval(TIMESYNCINTERVAL);  // Every 2 hours
  Serial.print("Unix time: ");
  Serial.println(now());
  display.println("NTP time set.");
  display.display();
  delay(1);

  // Get feeds for the first time
  while (nbrEvents == 0) {
    GetFeeds();
  }
}



//*********************************************************************/
// Arduino Loop Function
//*********************************************************************/
void loop() {
  // refresh time on LCD
  if (timeStatus() != timeNotSet) {
    //update the display only if time has changed (every seconds..)
    if (now() != prevDisplay) {
      prevDisplay = now();
      digitalClockDisplay(now());
    }
  }
  // is it time to refresh Ical feed?
  if ( ((millis() - lastConnectionTime) > connectionInterval) || (lastConnectionTime == 0)) {
    Serial.print("Last connection: ");
    Serial.print(millis() - lastConnectionTime);
    Serial.print(" / ");
    Serial.println(connectionInterval);
    GetFeeds();
  }

  // Display a complete loop of events.
  // Display on SSD1306 is for debug only: Slow & too small...
  //DisplayEventsOnSSD1306();
  // Production use : display on led 8X8
  DisplayEventsOnLed();
}
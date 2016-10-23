/*
 * Livermore Amateur Radio Klub (LART) APRS Tracker (LART/1)
 *
 * Provides (Automatic Position Reporting System) APRS tracker features that can be used 
 * as an GPS location transmitter "beacon" , and can also function as an  APRS packet receiver.     
 * The design is designed to integrate  the DRA818v/SA818v 2 meter 1 watt transceiver module, and
 * a NMEA compatible GPS module.  
 *
 * The following libraries are required (modified libraries are provided in the repository):
 * LibAPRS - provides the software TNC featuree
 * DRA818 - controls the  2 meter transceiver
 * TinyGPSplus - parses the GPS messages
 * LiquidCrystal_I2C (optional) - provides LCD display
 *
 * The hardware specifications and schematic are provided in the repository. It is designed to
 * run on a Arduino Mega2560 compatible board.
 * 
 * Author: David Fannin, KK6DF
 * Created: October 2016 
 * License: MIT License
 * Licenses and Copyright Notices for Modified Libraries are retained by the original authors. 
 *
 */

#if defined(__AVR_ATmega2560__) 
#define TARGET_CPU mega2560
#endif

#include "Arduino.h" 
#include "LART1_Settings.h"
#include "LiquidCrystal_I2C.h"
#include "LibAPRS.h"
#include "TinyGPSplus.h"
#include "DRA818.h"
#include "Log.h"

#define VERSION "Beta-0.999c"
#define ADC_REFERENCE REF_5V

// sets PTT pin (don't change, the pin is used by Port Manipulation later on) 
#define PTT 25

// usb serial port
HardwareSerial  * serialdb   = &Serial ;
// gps port
HardwareSerial  * serialgps  = &Serial1 ;
// DRA818 port
HardwareSerial * dra_serial = &Serial2 ;



unsigned long update_beacon = UPDATE_BEACON_INIT ;

unsigned long lastupdate = 0 ;

unsigned long lastupdatedisplay = 0 ;
bool forcedisplay = false ;

int sent_count=0 ;

#ifdef OPTION_LCD
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7 , 3, POSITIVE); 
#endif 

// GPS 

TinyGPSPlus gps ;

// DRA818
DRA818 dra(dra_serial, PTT);

// Log
//
Log mylog;

char buf[100] ;

boolean gotPacket = false ;
int recv_count = 0 ;
AX25Msg incomingPacket;
uint8_t *packetData;

void aprs_msg_callback(struct AX25Msg *msg)
{
   if(!gotPacket) {
      gotPacket = true ;
      memcpy(&incomingPacket, msg, sizeof(AX25Msg));
      if (freeMemory() > msg->len) {
         packetData = (uint8_t*) malloc(msg->len);
         memcpy(packetData,msg->info,msg->len);
         incomingPacket.info=packetData;
      } else {
         gotPacket = false ;
      }
   }
}


void processPacket()
{
   char tmpbuf[100] ; 
   if(gotPacket) {
      gotPacket = false ;
      recv_count++ ;

      if( gps.date.isValid() ) {
          sprintf(tmpbuf,"%4d-%02d-%02d %02d:%02d:%02d", 
                  gps.date.year(), gps.date.month(), gps.date.day(),
                  gps.time.hour(), gps.time.minute(), gps.time.second()
                 ) ;
      } 

      mylog.send(tmpbuf) ;

      sprintf(tmpbuf,"%s-%d %s-%d",
              incomingPacket.src.call, incomingPacket.src.ssid, 
              incomingPacket.dst.call, incomingPacket.dst.ssid );

      mylog.send(tmpbuf) ;

      int i = 0 ; 
      for(i = 0; i < incomingPacket.rpt_count ; i++) {
        sprintf(tmpbuf," %s-%d",incomingPacket.rpt_list[i].call, incomingPacket.rpt_list[i].ssid);
        mylog.send(tmpbuf) ;
      }

      for(i = 0; i < incomingPacket.len; i++) {
           tmpbuf[i]  = (char) incomingPacket.info[i] ;
      }

      tmpbuf[i] = '\0' ;
      mylog.send(tmpbuf) ;


      free(packetData) ;

      // serialdb->print(F("Free Ram:")) ;
      // serialdb->println(freeMemory()) ;

   }
}



void locationUpdate(const char *lat, const char *lon,int altitude, int height = 0 ,int power=1, int gain=0, int dir=0)
{
   char comment[43] ; // max of 36 chars with PHG data extension, 43 chars  without
   APRS_setLat(lat);
   APRS_setLon(lon);
   // APRS_setHeight(height);
   // APRS_setPower(power);
   // APRS_setGain(gain);
   // APRS_setDirectivity(dir);
   sprintf(comment,"/A=%06dLART-1 Tracker beta",altitude) ;
   APRS_sendLoc(comment, strlen(comment),serialdb);
}


double  ddtodmh(float dd) {

    double  value = dd ;
    double  degrees =  floor(value) ;
    value = ( value - degrees ) * 60  ;
    double  minutes =  floor(value ) ; 
    value =  (value - minutes) ;
    return ( degrees*100 ) + minutes + value ; 
}


void setup()
{

   dra_serial->begin(9600) ;
   serialgps->begin(4800) ;
   serialdb->begin(9600) ;

   lcd.begin(16,2);

   mylog.Log_Init(serialdb, &lcd) ;

   mylog.send(F("LART/1 APRS BEACON")) ;

   sprintf(buf,"CS:%s-%d",CALLSIGN,SSID);  
   mylog.send(buf) ;
   delay(1000UL) ;

   sprintf(buf,"Ver: %s",VERSION);  
   mylog.send(buf);
   delay(1000UL);

   // DRA818 Setup
   // must set these, then call WriteFreq
   //
   if ( dra.heartbeat() )  {
       mylog.send(F("Txcr OK Chk")) ;
   } else {
       mylog.send(F("Txcr Fail Chk")) ;
   } 
   delay(500UL);

   // set freq and tones
   dra.setFreq(TXFREQ,RXFREQ);
   dra.setTXCTCSS(TXCTCSS);
   dra.setSquelch(SQUELCH);
   dra.setRXCTCSS(RXCTCSS);
   dra.setBW(BW); // 0 = 12.5k, 1 = 25k
   if ( dra.writeFreq() ) {
        mylog.send(F("Freq Set"));
    } else {
        mylog.send(F("Freq Fail"));
    }
   delay(1000UL);

   if ( dra.setVolume(VOL) ) {
        mylog.send(F("Vol Set"));
    } else {
        mylog.send(F("Vol Fail"));

    } 


   delay(500UL);

   if ( dra.setFilters(FILTER_PREMPHASIS, FILTER_HIGHPASS, FILTER_LOWPASS) ) {
      mylog.send( F("Filter Set") );
   } else { 
      mylog.send( F("Filter Fail") );
   }

   delay(500UL);

   dra.setPTT(LOW);
   delay(500UL);

   mylog.send(F("APRS setup")) ;
   APRS_init(ADC_REFERENCE, OPEN_SQUELCH);
   APRS_setCallsign(CALLSIGN, SSID);
   APRS_setDestination(APRS_DEST,0);
   APRS_setPath1(PATH1,PATH1_SSID);
   APRS_setPath2(PATH2,PATH2_SSID);
   APRS_setPreamble(PREAMBLE);
   APRS_setTail(TAIL);
   APRS_setSymbol(SYMBOL);
   delay(500UL) ;

   mylog.send(F("Setup Complete")) ;
   delay(500UL) ;
   mylog.send(F("Ready")) ;

}

char  lat[] =  "0000.00N" ;
char  lon[] = "00000.00W" ;
int alt = 0 ;



void loop()
{

   while(serialgps->available() > 0 ) {
       if(gps.encode(serialgps->read())) {
           if( gps.location.isValid()) {
              double dmh ; 
               // latitude
               if ( gps.location.rawLat().negative ) {
                   dmh = ddtodmh( -gps.location.lat() )  ;
                   dtostrf(dmh,7,2,lat) ;
                   lat[7] = 'S';
               } else {
                   dmh = ddtodmh( gps.location.lat() )  ;
                   dtostrf(dmh,7,2,lat) ;
                   lat[7] = 'N';
               }
               // longitude
               if ( gps.location.rawLng().negative ) {
                   dmh = ddtodmh( -gps.location.lng() )  ;
                   dtostrf(dmh,8,2,lon) ;
                   lon[8] = 'W';
               } else {
                   dmh = ddtodmh(gps.location.lng() )  ;
                   dtostrf(dmh,8,2,lon) ;
                   lon[8] = 'E';
               }

           }

           // altitude 
           if ( gps.altitude.isValid() ) {
               alt = (int) gps.altitude.feet() ;
           }

       }
   }

   if ( millis() - lastupdate > update_beacon ) {
     lastupdate = millis() ;
     // const char * lat =  "3742.44N" ;
     // const char * lon = "12157.54W" ;
     // 

     if ( gps.location.isValid() )  {

         if ( --beacon_init_count <= 0 ) {
            update_beacon = UPDATE_BEACON ;
            beacon_init_count = 10000 ;
         }
         mylog.send(F("sending packet"));
         locationUpdate(lat,lon,alt,0,1,0,0) ;
         sent_count++ ;
         forcedisplay = true ; 
      }
   }

   processPacket() ;


   if ( forcedisplay ||  millis() - lastupdatedisplay > UPDATE_DISPLAY ) {
     lastupdatedisplay = millis() ;
         forcedisplay = false ;


         sprintf(buf,"s:%d r:%d",sent_count,recv_count) ;
         mylog.send(buf) ;

         if ( gps.location.isValid() )  {

             char datebuf[20] ; 

             sprintf(datebuf,"%4d-%02d-%02d %02d:%02d:%02d", 
                  gps.date.year(), gps.date.month(), gps.date.day(),
                  gps.time.hour(), gps.time.minute(), gps.time.second()
                 ) ;
            mylog.send(datebuf) ;
         } else {
            mylog.send(F("gps no fix")) ;
         }

         APRS_printSettings(serialdb) ;

     } 

}

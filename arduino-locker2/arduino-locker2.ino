#define DEBUG_MODE 0

#include <EEPROM.h>
#include "DHT.h"

#define DHTPIN 5 //DHT22 temperature and humidity sensor
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

const int MODULE_RESET_PIN = 6;    //reset button that must be HIGH
const int LED_A=10;
const int REED_SWITCH_PIN=11;      //Door CLOSED (magnet near)=LOW, Door OPEN (magnet away)=HIGH
const int MODULE_POWERON_PIN=12;   //make this pin serial with a 1-10 Ohm resistor and connect it to `power on` switch pin. note than idle=HIGH and for power on we should LOW for 1sec then HIGH.
const int LED_B=13;                //built-in LED
const int LED_C=9;                 //new LED
const int AnalogPinVoltMeter = A0;

char ch='\0';
String strBuf = "";
String strSender="";
String strBody="";
String strTargetNumber="+989133169571";
int intStepNo=0;
int intNextStepNo=0;
String strSms="";

int intErrCount=0;
int intErrCountSms=0;

int intTemperature=-1;
int intHumidity=-1;
int intTemperature2=-2;
int intHumidity2=-2;
float intVolt=-1;

int intReedState=0;
int intOldReedState=HIGH;                     //HIGH=1 Reed Switch not connected, Door is open
unsigned long intReedLastChangeTime=0;

int intPosB=0;
int intPosE=0;
int intPosE2=0;

bool isAlarmOn=false;
bool isStartupSmsSent=false;
unsigned long intSecCounter=0;
unsigned long intOldSecCounterMillis=0;
unsigned long intOldTime_ReadTemperatureAndVolt=0;
unsigned long intOldTime_ReadReedStatus=0;
unsigned long intOldTime_HourlySms=0;
unsigned long intOldTime_VoltageSms=0;

unsigned long intLedCount=0;
unsigned long intOldTime = 0;
unsigned long intElapsed = 0;
unsigned long intOldTimeLedA = 0;
 
#if DEBUG_MODE
int _oldStep=0;
#endif

/* ------functions------ */
int countCRLF(String str) {
  int count = 0;
  for (int i = 0; i < str.length() - 1; i++) {
    if (str[i] == '\r' && str[i + 1] == '\n') {
      count++;
      i++; // skip next character to avoid double-counting
    }
  }
  return count;
}


void setup() {
  pinMode(MODULE_RESET_PIN, OUTPUT);
  pinMode(MODULE_POWERON_PIN, OUTPUT);
  pinMode(LED_A, OUTPUT);
  pinMode(LED_C, OUTPUT);
  pinMode(REED_SWITCH_PIN, INPUT_PULLUP);  // use internal pull-up resistor
  
  digitalWrite(MODULE_RESET_PIN, HIGH);      //HIGH=idle (no reset)
  digitalWrite(MODULE_POWERON_PIN, HIGH);    //HIGH=idle (button released)
  digitalWrite(LED_A, LOW);
  
  Serial.begin(9600);  
  Serial1.begin(9600);  // SIM900 on pins 0/1 HW
  
//  while (!Serial);  // Wait for Serial Monitor to open (Leonardo)
  #if DEBUG_MODE
  Serial.println("SIM900 test ready! Type AT commands:");
  #endif

  dht.begin();
  
}

void loop() {
  /*------Read incoming serial data (non-blocking)------*/
  while (Serial1.available() > 0) {
    ch = Serial1.read();
    strBuf += ch;
    turnOnLedA();
    // prevent overflow
    if (strBuf.length() > 500) {
      strBuf = ""; // reset if something goes wrong
    }
  }

  #if DEBUG_MODE
  if (_oldStep!=intStepNo){
    Serial.println("stepno=" + String(intStepNo));
    _oldStep=intStepNo;
  }
  #endif
  
  intElapsed=millis()-intOldTime;
  switch(intStepNo){
    case 0:               //device start: wait 3 sec      
      turnOnLedA();
      goNext();
      break;
      
    case 1:      
      if (intElapsed>3000){                 //after 3sec send AT command to check if module is ON or OFF
        strBuf="";
        turnOnLedA();
        Serial1.println("AT");
        goNext();
      }
      break;

    case 2:      
      if (intElapsed>5000){                //after 5sec read the buffer to see if any response or not
        //check if any response
        if (strBuf.indexOf("OK\r\n")!=-1){
          //module is ON
          strBuf="";
          intStepNo=10;
        }else{
          //module is OFF so turn it ON
          #if DEBUG_MODE
          Serial.println("buf=" + strBuf);
          #endif
          turnOnLedA();
          digitalWrite(MODULE_POWERON_PIN, LOW);            //press the button down
          goNext();
        }
      }      
      break;

    case 3:       
      if (intElapsed>1500){                   //after 1.5sec release the button
        //check if any response
        turnOnLedA();
        digitalWrite(MODULE_POWERON_PIN, HIGH);            //release the button down
        goNext();
      }
      break;

    case 4:      
      if (intElapsed>15000){                   //after 12sec go from start and send AT command and check for OK
        turnOnLedA();
        intErrCount++;
        if (intErrCount>1){                    //module not response even after turn it ON, so critial error
          intStepNo=400;
        }else{          
          intStepNo=0;
        }        
      }
      break;

    case 10:
      //check if network is registered by sending AT+CREG?
      turnOnLedA();
      strBuf="";
      #if DEBUG_MODE
      Serial.println("sending AT+CREG...");
      #endif
      Serial1.println("AT+CREG?");      
      intNextStepNo=11;
      intStepNo=500;
      break;

    case 11:
      //check result of AT+CREG? 
      if (strBuf.indexOf("+CREG: 0,1")==-1){
        //not registered, so critical error and reboot
        intStepNo=400;
      }else{
        intStepNo=12;
      }      
      break;

    case 12:
      //del all sms
      strBuf="";
      strTargetNumber="+989133169571";        //set default receiver
      Serial1.println("AT+CMGD=1,4");
      intNextStepNo=13;
      intStepNo=500;
      break;
      
    case 13:
      //idle: read Received SMS and process and del / Reed sensor change / send sms every 1h / Reject RINGs / check antenna

      //count approximate seconds from startup
      if (millis()-intOldSecCounterMillis>1000){
        intOldSecCounterMillis=millis();
        intSecCounter++;
      }
      
      //Reed state change
      if (millis()-intOldTime_ReadReedStatus>100){
        intOldTime_ReadReedStatus=millis();
        intReedState=digitalRead(REED_SWITCH_PIN);
        if (intReedState!=intOldReedState) {
          if (millis()-intReedLastChangeTime>1000){                 //if changed and it is last more than 1 sec since last change, then change is not because of noise
            turnOnLedA();
            #if DEBUG_MODE
            Serial.println("Reed status changed to:" + String(intReedState) + " ("+strSender+") " + String(millis()));
            #endif
            if (intReedState==1) strSender="OPENED"; else strSender="CLOSED";
            intOldReedState=intReedState;
            intReedLastChangeTime=millis();
            if (isAlarmOn){                     //Alarm on state change
              //send sms then call
              strSms="!!!ALARM!!!\r\nDoor "+strSender;
              intNextStepNo=20;              //make a call
              intStepNo=600;                //send sms
            }
          }
        }else{        
          intReedLastChangeTime=millis();       //state is stable, reset timer to now for next change
        }
      }

      //read volt and temperature/humidity every 2min
      if (intStepNo==13){
        if (millis()-intOldTime_ReadTemperatureAndVolt>120000 || intOldTime_ReadTemperatureAndVolt==0){
          //read temperature and humidity
          intTemperature=round(dht.readTemperature());
          intHumidity=round(dht.readHumidity());
          delay(3000);
          intTemperature2=round(dht.readTemperature());
          intHumidity2=round(dht.readHumidity());
          if (intTemperature!=intTemperature2) intTemperature=-333;
          if (intHumidity!=intHumidity2) intHumidity=-333;
          //calc voltage
          int rawValue = analogRead(AnalogPinVoltMeter);
          float voltageAtPin = (rawValue / 1023.0) * 5;
          intVolt=voltageAtPin * (10000.0 + 4700.0) / 4700.0;
  
          intOldTime_ReadTemperatureAndVolt=millis();
        }
  
        //send battery voltage alarm
        if (intVolt<10.5){
          if (millis()-intOldTime_VoltageSms>600000){
            intOldTime_VoltageSms=millis();
            strSms="Battery decharge warning:\r\n";
            strSms+="\r\nVolt:"+String(intVolt);
            intNextStepNo=12;             //del all sms
            intStepNo=600;                //send sms
          }
        }
      }

      //send startup sms
      if (isStartupSmsSent==false){
        if (intSecCounter>2 && intStepNo==13){
          if (intOldReedState==1) strSender="OPEN"; else strSender="CLOSED";
          isAlarmOn=EEPROM.read(0);
          if (isAlarmOn) strSms="ON"; else strSms="OFF";
          strSms="Started\r\nAlarm is "+strSms+"\r\nDoor is "+strSender;
          strSms+="\r\nT:"+String(intTemperature)+" H:"+String(intHumidity);
          strSms+="\r\nVolt:"+String(intVolt);
          intNextStepNo=12;             //del all sms
          intStepNo=600;                //send sms
          isStartupSmsSent=true;
        }
      }
      
      //Read SMS on receive
      /*
       Receive SMS sample:
       +CMT: "+989133169571","","25/10/21,12:13:46+14"
       062A0633062A00200647064806340020
      */
      if (intStepNo==13){
        intPosB=strBuf.indexOf("+CMT: \"");
        if (intPosB!=-1){
          //sms is receiving, wait for 2 \r\n
          strBody=strBuf.substring(intPosB);
          if (countCRLF(strBody)>1){                //if strBuf from `+CMT: "` has 2 \r\n in it
            //sms receiving is completed
            delay(200);
            intPosB+=7;                             //after first double-quote
            intPosE=strBuf.indexOf('"',intPosB);    //next double-quote
            if (intPosE!=-1){
              //fetch sender number
              strSender=strBuf.substring(intPosB,intPosE);
              if (strSender.indexOf("+98")!=-1){
                strSender="0"+strSender.substring(3);
              }
              #if DEBUG_MODE
              Serial.println("sender detected:"+strSender);
              #endif
              intPosB=strBuf.indexOf("\r\n",intPosE);           //this \r\n is definitely exist, because this is first counted \r\n
              intPosB+=2;                                       //start of message body
              //intPosE=strBuf.length()-2;
              //strBody=strBuf.substring(intPosB,intPosE);
              intPosE=strBuf.indexOf("\r",intPosB);           //this enter is definitely exist. we need only line 1
              intPosE2=strBuf.indexOf("\n",intPosB);
              if (intPosE2<intPosE) intPosE=intPosE2;
              strBody=strBuf.substring(intPosB,intPosE);
              #if DEBUG_MODE
              Serial.println("{"+strSender+"} {"+strBody+"}");
              #endif
              //process received SMS:
              if (strSender=="09133169571" || strSender=="09031238058"){
                intNextStepNo=12;             //del all sms
                if (intOldReedState==1) strSender="OPEN"; else strSender="CLOSED";
                if (strBody=="0"){
                  //disable alarm
                  EEPROM.write(0,0);
                  strSms="Alarm ";
                  if (isAlarmOn==true){
                    isAlarmOn=false;
                    strSms+="turned";
                  }else{
                    strSms+="was already";
                  }
                  strSms+=" OFF.\r\nDoor is "+strSender+".";
                }else if(strBody=="1"){
                  //enable alarm
                  EEPROM.write(0,1);
                  strSms="Alarm ";
                  if (isAlarmOn==false){
                    //if door is open, do not turn alarm ON
                    if (intOldReedState==1){
                      strSms+="can not be enabled because ";
                    }else{
                      isAlarmOn=true;
                      strSms+="turned ON.\r\n";
                    }
                  }else{
                    strSms+="was already ON.\r\n";
                  }
                  strSms+="Door is "+strSender+".";
                }else if(strBody=="?"){
                  strSms="Alarm is ";
                  if (isAlarmOn==false) strSms+="OFF"; else strSms+="ON";
                  strSms+="\r\nDoor is "+strSender;
                  strSms+="\r\nT:"+String(intTemperature)+" H:"+String(intHumidity);
                  strSms+="\r\nVolt:"+String(intVolt);
                }else if(strBody=="r"){
                  strSms="Restarting...";                  
                  intNextStepNo=400;                //restart both SIM and Arduino
                }else{
                  //wrong command                
                  strBody=strBody.substring(0,140);             //max=160
                  strSms="Unknown command: "+strBody;                
                }
                intStepNo=600;                //send sms
              }else{
                #if DEBUG_MODE
                Serial.println("untrusted sender, so just del all sms.");
                #endif              
                intStepNo=12;
              }
            }else{
              //sms receiving error
              #if DEBUG_MODE
              Serial.println("SmsRcvErr: {"+strBuf+"}"+String(intPosB));
              #endif
              intStepNo=12;                       //del all sms
            }
          }
        }
      }


      //reject any RING
      if (intStepNo==13){
        intPosB=strBuf.indexOf("+CLIP: \"");
        if (intPosB!=-1){
          //incoming call, reject it
          strBuf="";
          Serial1.println("ath");
          intNextStepNo=14;
          intStepNo=500;
        }        
      }      
      
      //restart after 30days
      if (intStepNo==13){
        if (millis()>2592000000){       //2592000000=30days
          strSms="Restarting after 30days...";
          intNextStepNo=400;             //critical error: reset module then reset arduino
          intStepNo=600;                //send sms          
        }
        /*
        digitalWrite(LED_B, HIGH);
        digitalWrite(LED_C, HIGH);
        delay(50);
        digitalWrite(LED_B, LOW);
        digitalWrite(LED_C, LOW);
        delay(250);
        */
      }
      if (intStepNo==13 && isStartupSmsSent){
        if (millis()-intOldTime_HourlySms>3600000 || intOldTime_HourlySms==0){
          intOldTime_HourlySms=millis();
          strSms="Alarm is ";
          if (isAlarmOn==false) strSms+="OFF"; else strSms+="ON";
          strSms+="\r\nDoor is "+strSender;
          strSms+="\r\nT:"+String(intTemperature)+" H:"+String(intHumidity);
          strSms+="\r\nVolt:"+String(intVolt);
          strTargetNumber="+989352628356";
          intNextStepNo=12;
          intStepNo=600;              //send sms
        }
      }
      break;

    case 14:
      strBuf="";
      intStepNo=13;
      break;

    case 20:
      //make a call because of ALARM after 10sec (from sending sms)
      if (intElapsed>10000){
        strBuf="";
        Serial1.println("atd+989133169571;");
        intNextStepNo=21;
        intStepNo=500;
      }
      turnOnLedA();
      break;
      
    case 21:      
      if (intElapsed>40000){              //40sec wait for call to be made, then hangup
        strBuf="";
        Serial1.println("ath");
        intNextStepNo=13;
        intStepNo=500;
      }
      turnOnLedA();
      break;
      

    /*---------------------------------*/
    /*critical error: blink 10 times then reboot*/
    case 400:
      //reset module
      digitalWrite(MODULE_RESET_PIN,LOW);
      delay(1000);
      digitalWrite(MODULE_RESET_PIN,HIGH);
      delay(5000);    
      for (intErrCount=0;intErrCount<10;intErrCount++){
        digitalWrite(LED_A, HIGH);
        digitalWrite(LED_B, HIGH);
        digitalWrite(LED_C, HIGH);
        delay(1000);
        digitalWrite(LED_A, LOW);
        digitalWrite(LED_B, LOW);
        digitalWrite(LED_C, LOW);
        delay(1000);
      }
      #if DEBUG_MODE
      intStepNo=0;
      #else
      ((void(*)())0)();   // direct jump to address 0 → reset      
      intStepNo=999;
      #endif
      break;
    
    /*---------------------------------*/
    /*wait for module to finish response*/
    case 500:
      intErrCount=0;
      goNext();
      break;

    case 501:
      if (intElapsed<5000){
        if (isResponseComplete(strBuf)){
          #if DEBUG_MODE
          Serial.println("strBuf={" + strBuf + "} ok");
          #endif
          goNext();
        }
      }else{
        #if DEBUG_MODE
        Serial.println("strBuf={" + strBuf + "} more than 5sec");
        #endif
        intErrCount++;
        if (intErrCount>6){           //wait 30sec for AT command EOF
          intStepNo=400;              //blink 10 times then reboot
        }else{
          intOldTime=millis();
        }
      }
      turnOnLedA();
      break;

    case 502:
      if (intElapsed>500){          //wait 500ms after AT command EOF
        intOldTime=millis();
        intStepNo=intNextStepNo;
      }
      break;

    /*---------------------------------*/
    /*send sms*/
    case 600:
      intErrCountSms=0;
      goNext();
      break;
      
    case 601:
      #if DEBUG_MODE
      Serial.println("send SMS: " + strSms);      
      #endif
      strBuf="";
      Serial1.println("AT+CMGS=\""+strTargetNumber+"\"");
      intOldTime=millis();
      intStepNo=602;      
      break;

    case 602:
      if (intElapsed<3000){
        if (strBuf.indexOf("> ")!=-1){        //sms send prompt ok
          Serial1.print(strSms+"\r\n"+millis());
          delay(300);
          Serial1.write(26);
          intOldTime=millis();
          intStepNo=603;          
        }
      }else{
        //wait for SMS > sign but nothing
        intStepNo=400;
      }
      break;

    case 603:
      if (intElapsed<60000){              //40sec wait for sms send
        if (strBuf.indexOf("\r\nOK\r\n")!=-1){
          //sms sent successfully
          intStepNo=intNextStepNo;
        }else if (strBuf.indexOf("ERROR")!=-1){
          //sms sent error
          Serial.println("sms send failed No "+String(intErrCountSms)+"/3 strBuf={"+strBuf+"}");
          intErrCountSms++;
          if (intErrCountSms<3){
            intStepNo=601;
          }else{
            intStepNo=400;
          }
        }
        turnOnLedA();
      }else{
        //sms not sent after 15sec, so retry 3 times
        Serial.println("sms send failed No "+String(intErrCountSms)+"/3 strBuf={"+strBuf+"}");
        intErrCountSms++;
        if (intErrCountSms<3){
          intStepNo=601;
        }else{
          intStepNo=400;
        }
      }
      break; 
    
  }

  //blink onboad LED
  intLedCount++;
  if (intLedCount==5000){
    digitalWrite(LED_B, HIGH);
    digitalWrite(LED_C, HIGH);
  }else if (intLedCount==5100){
    digitalWrite(LED_B, LOW);
    digitalWrite(LED_C, LOW);
    intLedCount=0;
  }

  //turn off LED-A
  if (intOldTimeLedA!=0){
    if (millis()-intOldTimeLedA>100){
      digitalWrite(LED_A, LOW);
    }
  }
  
  /*
  if (Serial1.available()) {
    char c = Serial1.read();

    // Check for special commands
    if (c == '_') {
      digitalWrite(MODULE_POWERON_PIN, HIGH);  // Turn pin 9 ON
      Serial1.println("[HIGH=idle]");
      // Do NOT send to SIM900
    }
    else if (c == '-') {
      digitalWrite(MODULE_POWERON_PIN, LOW);   // Turn pin 9 OFF
      Serial1.println("[LOW=turn on]");
      // Do NOT send to SIM900
    }
    else if (c == '$') {
      int state = digitalRead(REED_SWITCH_PIN);
      if (state == LOW) {
        Serial1.println("Door CLOSED (magnet near)");
      } else {
        Serial1.println("Door OPEN (magnet away)");
      }      
    }
    else {
      // Send any other character to SIM900
      Serial1.write(c);
    }
  }

  // Read characters from SIM900 and forward to Serial Monitor
  if (Serial1.available()) {
    Serial1.write(Serial1.read());
  }
  */
}


void turnOnLedA()
{
  digitalWrite(LED_A, HIGH);
  intOldTimeLedA=millis();
}

void goNext(){
  intOldTime=millis();
  intStepNo++;
}

bool isResponseComplete(const String &buf) {
  if (buf.endsWith("\r\nOK\r\n")) return true;
  if (buf.endsWith("\r\nERROR\r\n")) return true;
  if (buf.indexOf("+CME ERROR:") != -1) return true;
  if (buf.indexOf("+CMS ERROR:") != -1) return true;
  return false;
}

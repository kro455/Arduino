#include <MemoryUsage.h>
#include <DS3231.h>
#include <LiquidCrystal.h>
#include <IRremote.h>
#include <EEPROM.h>

#define MAX_LINE_LCD 2
#define SDA_PIN 2
#define SCL_PIN 3
#define BUTTON 4
#define BUZZER_PIN 5
#define RADAR_OUT_PIN 6
#define RECEIVER_PIN 7
#define SUPER_LED_PIN 9
#define LED_LCD_A_PIN 10
#define EEPROM_Addr_ALARM_SIZE 255
#define EEPROM_Addr_SMART_MODE 200
#define MAX_ALARM_SIZE 15
#define MAX_TIME 11000

byte brightness = 15;
byte brightnessSuperLED = 0;
bool PowerON = true;
bool superLedON = false;

bool alarmON = false;
bool smartON = false;
byte ptime = 99;

IRrecv receiver(RECEIVER_PIN);
LiquidCrystal lcd = LiquidCrystal(A0, A1, A2, A3, A4, A5);  // LiquidCrystal(rs,en,d4,d5,d6,d7);
DS3231 rtc(SDA_PIN, SCL_PIN);

struct Alarm {
  byte hour;
  byte minute;
  byte repeat = B00000000;
  bool status = true;
  Alarm() {
    this->hour = 0;
    this->minute = 0;
  }

  Alarm(byte hour, byte min) {
    this->hour = hour;
    this->minute = min;
  }

  void setRepeat(byte dow) {
    bitWrite(repeat, dow, bitRead(repeat, dow) == 1 ? 0 : 1);
    Serial.println(repeat, BIN);
  }

  byte getRepeat(byte dow) {
    return bitRead(repeat, dow);
  }
  String getRepeatStr(byte dow) {
    return bitRead(repeat, dow) ? F(" ON") : F(" OFF");
  }

  void setStatus() {
    status = status ? false : true;
    Serial.println("Status: " + String(status));
  }

  String getStatus() {
    return (status ? F("ON") : F("OFF"));
  }

  Alarm subtractTime(int min) {
    Alarm result;
    int resultHour = hour;
    int resultMinute = minute - min;
    if (resultMinute < 0) {
      resultHour--;
      resultMinute += 60;
    }
    if (resultHour < 0) {
      resultHour += 24;
    }
    result.hour = resultHour;
    result.minute = resultMinute;
    return result;
  }

  String toString() {
    return ((hour < 10) ? "0" : "") + String(hour) + F(":") + ((minute < 10) ? F("0") : F("")) + String(minute);
  }
  String toStringAndStatus() {
    return toString() + F(" ") + getStatus();
  }
};

struct SmartMode {
  byte mode;
  byte lightTime;
  byte sensitivity;
  byte closeMode;
  byte scored;

  SmartMode() {
    mode = 0;
    lightTime = 30;
    sensitivity = 1;
    closeMode = 0;
    scored = 0;
  }
  void setMode() {
    mode = (mode + 1) % 3;
  }
  void setSensitivity() {
    sensitivity += (sensitivity > 0) ? -1 : 2;
  }
  void setCloseMode() {
    closeMode = (closeMode + 1) % 3;
  }

  String getMode() {
    switch (mode) {
      case 0: return F(" Normal");
      case 1: return F(" Simple");
      case 2: return F(" Smart ");
    }
  }
  String getSensitivity() {
    switch (sensitivity) {
      case 0: return F("  HIGH ");
      case 1: return F(" Normal");
      case 2: return F("  LOW  ");
    }
  }
  String getCloseMode() {
    switch (closeMode) {
      case 0: return F("Normal");
      case 1: return F("Gaming");
      case 2: return F("Moving");
    }
  }
  String getScored() {
    return String(scored) + ((closeMode == 2) ? F("Min") : F(""));
  }
};

Alarm currentAlarm;
Alarm smartAlarm;
byte EEPROM_index = -1;
SmartMode smartMode;
byte alarmSize = 0;

void setup() {
  Serial.begin(115200);
  setupLCD();
  rtc.begin();
  EEPROM.get(EEPROM_Addr_ALARM_SIZE, alarmSize);
  EEPROM.get(EEPROM_Addr_SMART_MODE, smartMode);
  findNextAlarm();
  //rtc.setTime(13, 1, 50);
  //rtc.setDate(21, 1, 2024);
  receiver.enableIRIn();  // start the IR receiver
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RADAR_OUT_PIN, INPUT);
  pinMode(BUTTON, INPUT_PULLUP);
}
void loop() {
  movingTargetDetected();
  daemon_Alarm();
  displayTime();
  char key = getKey();
  switch (key) {
    case 'M':
      menuHome();
      break;
    case 'T':
      Serial.println(currentAlarm.toString());
      Serial.println(calculateTimeToNextAlarm(currentAlarm));
      break;
  }
}

void menuHome() {
  String option[] = { F("Setting"), F("Set Alarm"), F("Alarm Mode"), F("Testing Mode") };
  menuExecFunc(option, sizeof(option) / sizeof(option[0]), menuFuncHome);
}

void menuFuncHome(byte number) {
  switch (number) {
    case 0:
      menuSetting();
      break;
    case 1:
      menuAlarm();
      findNextAlarm();
      break;
    case 2:
      alarmMode();
      break;
    case 3:
      dinoGame(0);
      setup();
      break;
  }
}

void daemon_Alarm() {
  Time t = rtc.getTime();
  if (currentAlarm.status == true && ((currentAlarm.getRepeat(t.dow) == 1 && currentAlarm.getRepeat(0) == 1) || currentAlarm.getRepeat(0) == 0)
      && currentAlarm.hour == t.hour && currentAlarm.minute == t.min) {
    alarmON = true;
  }
  // FIXME: still have bug not fixed yet
  if (smartMode.mode == 1) {  // simple mode 
    smartAlarm = currentAlarm.subtractTime(smartMode.lightTime);
    if (smartAlarm.status == true && ((smartAlarm.getRepeat(t.dow) == 1 && smartAlarm.getRepeat(0) == 1) || smartAlarm.getRepeat(0) == 0)
        && smartAlarm.hour == t.hour && smartAlarm.minute >= t.min) {
      smartON = true;
    }
  }
  alarmActice();
}
void alarmActice() {
  static byte last_min = rtc.getTime().min;
  analogWrite(SUPER_LED_PIN, brightnessSuperLED);
  if (alarmON) {
    SUPER_LED_ON();
    beep(50);
    if (rtc.getTime().min != last_min) {
      last_min = rtc.getTime().min;
      if (255 - (255 / smartMode.lightTime) > brightnessSuperLED)
        brightnessSuperLED += 255 / smartMode.lightTime;
    }
    if (getKey() != 0 || (digitalRead(BUTTON) == LOW)) {
      brightnessSuperLED = 1;
      analogWrite(SUPER_LED_PIN, brightnessSuperLED);
      switch (smartMode.closeMode) {
        case 1:
          dinoGame(smartMode.scored);
          setup();
          break;
        case 2:
          keepMoving(smartMode.scored);
          break;
      }
      lcd.clear();
      lcd.print(F("Good Moring!"));
      alarmON = false;
      smartON = false;
      brightnessSuperLED = 0;
      superLedON = false;
      digitalWrite(BUZZER_PIN, LOW);
      if (currentAlarm.getRepeat(0) == 0) {
        currentAlarm.status = false;
        EEPROMsetAlarm(EEPROM_index, currentAlarm);
      };
      lcd.setCursor(0, 1);
      if (findNextAlarm()) {
        lcd.print(F("Next Alarm:"));
        lcd.print(currentAlarm.toString());
      } else {
        lcd.print(F("No further alarm"));
      }
      delay(2000);
      lcd.clear();
    }
  }
}

int calculateTimeToNextAlarm(Alarm alarm) {
  if (!alarm.status) return -1;  // alarm is OFF
  bool nextday = false;
  Time t = rtc.getTime();
  int timeToNextAlarm = 0;
  if (alarm.hour > t.hour || (alarm.hour == t.hour && alarm.minute > t.min)) {
    timeToNextAlarm += (alarm.hour - t.hour) * 60 + (alarm.minute - t.min);  //time to next hour
  } else {
    timeToNextAlarm = (24 - t.hour + alarm.hour) * 60 + (alarm.minute - t.min);
    nextday = true;
  }
  if (alarm.getRepeat(0) == 0) {
    return timeToNextAlarm;  // alarm repeat 1 times!
  }
  for (int i = 1; i < 8; i++) {
    int dow = t.dow + i;
    while (dow > 7) dow -= 7;
    Serial.println("dow: " + String(dow));
    Serial.println(alarm.getRepeat(dow));
    if (alarm.getRepeat(dow) == 1) {
      return timeToNextAlarm + (nextday ? i - 1 : i) * 24 * 60;
    }
  }
  return -1;
}

bool findNextAlarm() {
  int min_time = MAX_TIME;
  int index_min = 0;
  for (int i = 0; i < alarmSize; i++) {
    Alarm a = EEPROMgetAlarm(i);
    if (!a.status) continue;
    int time = calculateTimeToNextAlarm(a);
    if (time < min_time) {
      min_time = time;
      index_min = i;
    }
  }
  if (min_time < MAX_TIME) {
    currentAlarm = EEPROMgetAlarm(index_min);
    EEPROM_index = index_min;
    return true;
  }
  currentAlarm = Alarm(24, 0);
  EEPROM_index = -1;
  return false;
}

void SUPER_LED_ON() {
  if (!superLedON) {
    superLedON = true;
    analogWrite(SUPER_LED_PIN, 255);
    analogWrite(SUPER_LED_PIN, 0);
    delay(50);
    analogWrite(SUPER_LED_PIN, 255);
    analogWrite(SUPER_LED_PIN, 0);
    delay(50);
    analogWrite(SUPER_LED_PIN, brightnessSuperLED);
  }
}

unsigned long last_read = millis();
void movingTargetDetected() {
  static unsigned long time_detected_moving = 0;
  static bool is_moving = false;
  static unsigned long data = 0;
  static unsigned long total = 0;
  static int sum = 0;
  if (digitalRead(RADAR_OUT_PIN) == HIGH) {
    if (!is_moving) {
      is_moving = true;
      time_detected_moving = millis();
    }
  } else if (is_moving) {
    is_moving = false;
    data = (millis() - time_detected_moving) / 1000;
    total += data;
    // Serial.print("data: "));
    // Serial.println(data);
  }
  if (millis() - last_read > 300000) {
    last_read = millis();
    Serial.print(F("Total: "));
    Serial.println(total);
  }
  if (smartON == true && is_moving && ((millis() - time_detected_moving) / 1000) > smartMode.sensitivity * 5) {
    alarmON = true;
  }
  //return is_moving ? (millis() - time_detected_moving) / 1000 : 0;
}

void alarmMode() {
  char index_choice = 0;
  byte index_display = 0;
  byte length_of_option = 5;
  while (true) {
    String option[] = { "Mode:" + smartMode.getMode(),
                        "Light: " + String(smartMode.lightTime) + F(" Min"),
                        "Sens :" + smartMode.getSensitivity(),
                        "CloseM:" + smartMode.getCloseMode(),
                        "Scored:" + smartMode.getScored() };
    if (smartMode.closeMode == 0) length_of_option = 4;
    else length_of_option = 5;
    displayMenu(index_choice, index_display, option, length_of_option);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        alarmChangeMode(index_choice);
        break;
      case '+':
        if (--index_choice < 0) { index_choice += length_of_option; }
        break;
      case '-':
        if (++index_choice == length_of_option) { index_choice = 0; }
        break;
      case 'B':
        return;
    }
    if (index_choice >= (index_display + MAX_LINE_LCD)) {
      index_display = index_choice - MAX_LINE_LCD + 1;
    } else if (index_choice < index_display) {
      index_display = index_choice;
    }
  }
}

void alarmChangeMode(byte index) {
  switch (index) {
    case 0:
      smartMode.setMode();
      break;
    case 1:
      setLightTime();
      break;
    case 2:
      smartMode.setSensitivity();
      break;
    case 3:
      smartMode.setCloseMode();
      break;
    case 4:
      setScored();
      break;
  }
  EEPROM.put(EEPROM_Addr_SMART_MODE, smartMode);
}

void setScored() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Scored of "));
  lcd.print(smartMode.closeMode == 1 ? F("Gaming") : F("Moving"));
  lcd.setCursor(0, 1);
  lcd.print(F("current: "));
  while (true) {
    lcd.setCursor(9, 1);
    lcd.print(smartMode.scored);
    lcd.print(F(" "));
    char key = getKeyNotBlank();
    switch (key) {
      case '+':
        if (smartMode.scored < 20)
          smartMode.scored++;
        else
          lcd.print(F("MAX"));
        break;
      case '-':
        if (smartMode.scored > 1)
          smartMode.scored--;
        else
          lcd.print(F("MIN"));
        break;
      case 'B':
        return;
    }
    lcd.print(F("   "));
  }
}
void setLightTime() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("   LightTime   "));
  lcd.setCursor(0, 1);
  lcd.print(F("current: "));
  while (true) {
    lcd.setCursor(9, 1);
    lcd.print(smartMode.lightTime);
    lcd.print(F(" "));
    char key = getKeyNotBlank();
    switch (key) {
      case '+':
        if (smartMode.lightTime < 60)
          smartMode.lightTime++;
        else
          lcd.print(F("MAX"));
        break;
      case '-':
        if (smartMode.lightTime > 0)
          smartMode.lightTime--;
        else
          lcd.print(F("MIN"));
        break;
      case 'B':
        return;
    }
    lcd.print(F("   "));
  }
}

void testingMode() {
  String option[] = { F("Testing BUTTON"), F("Testing BUZZER"), F("Testing Radar"),
                      F("Testing Remote"), F("Testing Super LED"), F("Testing Dino game") };
  menuExecFunc(option, sizeof(option) / sizeof(option[0]), testingModeFunc);
}

void testingModeFunc(byte index) {
  switch (index) {
    case 0:
      //testButton();
      break;
    case 1:
      break;
    case 2:
      break;
    case 3:
      break;
    case 4:
      SUPER_LED_ON();
      delay(2000);
      analogWrite(SUPER_LED_PIN, 0);
      break;
    case 5:
      dinoGame(10);
      setup();
      break;
  }
}

void menuAlarm() {
  char index_choice = 0;
  byte index_display = 0;
  String option[MAX_ALARM_SIZE + 1];
  option[0] = F("New Alarm");
  while (true) {
    for (int i = 0; i < alarmSize; i++) {
      option[i + 1] = EEPROMgetAlarm(i).toStringAndStatus();
    }
    displayMenu(index_choice, index_display, option, alarmSize + 1);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        menuFuncAlarm(index_choice);
        break;
      case '+':
        if (--index_choice < 0) { index_choice += alarmSize + 1; }
        break;
      case '-':
        if (++index_choice == alarmSize + 1) { index_choice = 0; }
        break;
      case 'B':
        return;
      default:
        char index = key - '0' - 1;
        if (index >= 0 && index < alarmSize + 1) {
          menuFuncAlarm(index);
        }
    }
    if (index_choice >= (index_display + MAX_LINE_LCD)) {
      index_display = index_choice - MAX_LINE_LCD + 1;
    } else if (index_choice < index_display) {
      index_display = index_choice;
    }
  }
}

void menuFuncAlarm(byte number) {
  switch (number) {
    case 0:
      addNewAlarm();
      if (settingAlarm(alarmSize - 1) == 0)
        deleteAlarm(alarmSize - 1);
      break;
    default:
      if (number > 0 && number <= alarmSize) {
        settingAlarm(number - 1);
      }
  }
}

int settingAlarm(byte index) {
  currentAlarm = EEPROMgetAlarm(index);
  char index_choice = 0;
  byte index_display = 0;
  byte length_of_option = 4;
  while (true) {
    String option[] = { "Status: " + currentAlarm.getStatus(), F("Set Time"), F("Set Repeat"), F("Delete Alarm") };
    //Serial.println((int)index_choice);
    displayMenu(index_choice, index_display, option, length_of_option);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        switch (index_choice) {
          case 0:
            currentAlarm.setStatus();
            break;
          case 1:
            setAlarm();
            break;
          case 2:
            setRepeat();
            break;
          case 3:
            lcd.clear();
            lcd.print(F("  Delete Alarm? "));
            lcd.setCursor(0, 1);
            lcd.print(F(" Yes(E) / No(B) "));
            if (getKeyNotBlank() == 'E') {
              deleteAlarm(index);
              return -1;
            }
        }
        break;
      case '+':
        if (--index_choice < 0) { index_choice += length_of_option; }
        break;
      case '-':
        if (++index_choice == length_of_option) { index_choice = 0; }
        break;
      case 'B':
        lcd.clear();
        lcd.print(F("Save? Yes(Enter)"));
        lcd.setCursor(0, 1);
        lcd.print(F("No(C) /Cancel(B)"));
        char key = getKeyNotBlank();
        if (key == 'E') {
          EEPROMsetAlarm(index, currentAlarm);
          return 1;
        } else if (key == 'C') return 0;
    }
    if (index_choice >= (index_display + MAX_LINE_LCD)) {
      index_display = index_choice - MAX_LINE_LCD + 1;
    } else if (index_choice < index_display) {
      index_display = index_choice;
    }
  }
}

void setRepeat() {
  char index_choice = 0;
  byte index_display = 0;
  byte length_of_option = 8;
  while (true) {
    String option[8] = { "Power:" + currentAlarm.getRepeatStr(0),
                         "Mon:" + currentAlarm.getRepeatStr(1),
                         "Tue:" + currentAlarm.getRepeatStr(2),
                         "Wed:" + currentAlarm.getRepeatStr(3),
                         "Thu:" + currentAlarm.getRepeatStr(4),
                         "Fri:" + currentAlarm.getRepeatStr(5),
                         "Sat:" + currentAlarm.getRepeatStr(6),
                         "Sun:" + currentAlarm.getRepeatStr(7) };
    displayMenu(index_choice, index_display, option, length_of_option);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        currentAlarm.setRepeat(index_choice);
        break;
      case '+':
        if (--index_choice < 0) { index_choice += length_of_option; }
        break;
      case '-':
        if (++index_choice == length_of_option) { index_choice = 0; }
        break;
      case 'B':
        return;
    }
    if (index_choice >= (index_display + MAX_LINE_LCD)) {
      index_display = index_choice - MAX_LINE_LCD + 1;
    } else if (index_choice < index_display) {
      index_display = index_choice;
    }
  }
}

void addNewAlarm() {
  if (alarmSize < MAX_ALARM_SIZE) {
    Alarm newAlarm(0, 0);
    alarmSize++;
    EEPROMsetAlarm(alarmSize - 1, newAlarm);
    EEPROM.update(EEPROM_Addr_ALARM_SIZE, alarmSize);
  } else {
    lcd.setCursor(0, 0);
    lcd.print(F("No more space!"));
    delay(2000);
  }
}

Alarm EEPROMgetAlarm(byte index) {
  if (index < alarmSize)
    EEPROM.get(index * sizeof(Alarm), currentAlarm);
  return currentAlarm;
}

void EEPROMsetAlarm(byte index, Alarm alarm) {
  if (index < alarmSize) {
    EEPROM.put(index * sizeof(Alarm), alarm);
  }
}

void setAlarm(byte index) {
  EEPROMgetAlarm(index);
  setAlarm();
}

void setAlarm() {
  String option[] = { F(" Hour"), F(" Min") };
  methodExecFunc(option, sizeof(option) / sizeof(option[0]), setAlarmFunc, displayCurrentAlarm);
}

void deleteAlarm(byte index) {
  if (index >= 0 && index < alarmSize) {
    Alarm arrAlarm[alarmSize];
    for (int i = 0; i < alarmSize; i++) {
      arrAlarm[i] = EEPROMgetAlarm(i);
    }
    for (int i = index; i < alarmSize - 1; i++) {
      arrAlarm[i] = arrAlarm[i + 1];
    }
    alarmSize--;
    for (int i = 0; i < alarmSize; i++) {
      EEPROMsetAlarm(i, arrAlarm[i]);
    }
    EEPROM.update(EEPROM_Addr_ALARM_SIZE, alarmSize);
  }
}

void displayCurrentAlarm() {
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print(F("Alarm: "));
  lcd.print(currentAlarm.toString());
}

void setAlarmFunc(byte index, char key) {
  switch (index) {
    case 0:
      if (key == '+') currentAlarm.hour += (currentAlarm.hour < 23) ? 1 : -23;
      else if (key == '-') currentAlarm.hour += (currentAlarm.hour > 0) ? -1 : 23;
      break;
    case 1:
      if (key == '+') currentAlarm.minute += (currentAlarm.minute < 59) ? 1 : -59;
      else if (key == '-') currentAlarm.minute += (currentAlarm.minute > 0) ? -1 : 59;
      break;
  }
}

void menuSetting() {
  String option[] = { F("Brightness"), F("Change Time"), F("Change Date") };
  menuExecFunc(option, sizeof(option) / sizeof(option[0]), menuFuncSetting);
}

void menuFuncSetting(byte number) {
  switch (number) {
    case 0:
      changeBrightness();
      break;
    case 1:
      changeTime();
      break;
    case 2:
      changeDate();
      break;
  }
}

void changeTime() {
  char newHour[] = "__";
  char newMin[] = "__";
  byte index = 0;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Current : "));
  lcd.setCursor(0, 1);
  lcd.print(F("New Time: "));
  while (true) {
    displayTime(10, 0);
    lcd.setCursor(10, 1);
    lcd.print(newHour);
    lcd.print(F(":"));
    lcd.print(newMin);
    char key = getKey();
    if (key) {
      bool flag = false;
      if (index < 4) {
        //validation digit
        switch (index) {
          case 0:
            if (key >= '0' && key <= '2') flag = true;
            break;
          case 1:
            if (newHour[0] < '2') {
              if (key >= '0' && key <= '9') flag = true;
            } else {
              if (key >= '0' && key < '4') flag = true;
            }
            break;
          case 2:
            if (key >= '0' && key <= '5') flag = true;
            break;
          case 3:
            if (key >= '0' && key <= '9') flag = true;
            break;
        }
        //input number
        if (flag) {
          if (index < 2) {
            newHour[index] = key;
          } else {
            newMin[index - 2] = key;
          }
          index++;
        }
      }
      switch (key) {
        case 'E':  //enter key.
          setTime(valueOf(newHour), valueOf(newMin));
          return;
        case 'C':  //delete key.
          if (index > 0) {
            index--;
            if (index < 2) {
              newHour[index] = '_';
            } else {
              newMin[index - 2] = '_';
            }
          }
          break;
        case 'B':  // back key.
          return;
        default:
          if (!flag) beep();
      }
    }
  }
}

void setTime(int hour, int min) {
  if (hour > -1 && min > -1) {
    rtc.setTime(hour, min, 0);
    displayTime(10, 0);
    lcd.setCursor(0, 1);
    lcd.print(F("   successful   "));
  } else {
    lcd.setCursor(0, 1);
    lcd.print(F("     Failed     "));
  }
  getKeyNotBlank();
}

int valueOf(char str[]) {
  byte i = 0;
  int number = 0;
  while (str[i] == '0' && str[i] != '\0') i++;
  while (str[i] != '\0') {
    if (str[i] >= '0' && str[i] <= '9')
      number = number * 10 + str[i++] - '0';
    else return -1;
  }
  return number;
}

void displayTime(byte x, byte y) {
  lcd.setCursor(x, y);
  Time t = rtc.getTime();
  if (t.hour < 10) lcd.print(F("0"));
  lcd.print(t.hour);
  lcd.print(F(":"));
  if (t.min < 10) lcd.print(F("0"));
  lcd.print(t.min);
}

void changeDate() {
  String option[] = { F(" Date"), F("Month"), F(" Year") };
  methodExecFunc(option, sizeof(option) / sizeof(option[0]), changeDateFunc, displayCurrentDate);
}

void displayCurrentDate() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("DATE: "));
  lcd.print(rtc.getDateStr(FORMAT_LONG, FORMAT_LITTLEENDIAN, 165));
}

void changeDateFunc(byte index, char key) {
  switch (index) {
    case 0:
      if (key == '+') rtc.setDate(rtc.getTime().date + 1);
      else if (key == '-') rtc.setDate(rtc.getTime().date - 1);
      break;
    case 1:
      if (key == '+') rtc.setMon(rtc.getTime().mon + 1);
      else if (key == '-') rtc.setMon(rtc.getTime().mon - 1);
      break;
    case 2:
      if (key == '+') rtc.setYear(rtc.getTime().year + 1);
      else if (key == '-') rtc.setYear(rtc.getTime().year - 1);
      break;
  }
  rtc.setDOW();
}

void methodExecFunc(String option[], int length_of_option, void (*execFunc)(int, char), void (*displayFunc)()) {
  char index_choice = 0;
  while (true) {
    //MEMORY_PRINT_FREERAM
    displayFunc();
    displayMethod(index_choice, option);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        // never coming :)
        break;
      case '+':
      case '-':
        execFunc(index_choice, key);
        break;
      case '<':
        if (--index_choice < 0) index_choice += length_of_option;
        break;
      case '>':
        if (++index_choice == length_of_option) index_choice = 0;
        break;
      case 'B':
        return;
    }
  }
}

void displayMethod(byte index_choice, String option[]) {
  lcd.setCursor(0, 1);
  lcd.print(F(" "));
  lcd.print((char)127);
  lcd.print(F(" "));
  lcd.print(option[index_choice]);
  lcd.setCursor(9, 1);
  lcd.write(byte(0));
  lcd.write(byte(1));
  lcd.print(F(" "));
  lcd.print((char)126);
}

void menuExecFunc(String option[], int length_of_option, void (*execFunc)(int)) {
  char index_choice = 0;
  byte index_display = 0;
  while (true) {
    //Serial.println((int)index_choice);
    displayMenu(index_choice, index_display, option, length_of_option);
    char key = getKeyNotBlank();
    switch (key) {
      case 'E':
        execFunc(index_choice);
        break;
      case '+':
        if (--index_choice < 0) { index_choice += length_of_option; }
        break;
      case '-':
        if (++index_choice == length_of_option) { index_choice = 0; }
        break;
      case 'B':
        return;
      default:
        char index = key - '0' - 1;
        if (index >= 0 && index < length_of_option) {
          execFunc(index);
        }
    }
    if (index_choice >= (index_display + MAX_LINE_LCD)) {
      index_display = index_choice - MAX_LINE_LCD + 1;
    } else if (index_choice < index_display) {
      index_display = index_choice;
    }
  }
}

void displayMenu(byte index_index_choice, byte index_display, String option[], byte n) {
  lcd.clear();
  for (byte i = 0; i < MAX_LINE_LCD; i++) {
    if (index_display + i <= n - 1) {
      lcd.setCursor(1, i);
      lcd.print(index_display + i + 1);
      lcd.print(F(":"));
      lcd.print(option[index_display + i]);
    }
  }
  lcd.setCursor(0, index_index_choice - index_display);
  lcd.print((char)126);
}

void changeBrightness() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("   Brightness   "));
  lcd.setCursor(0, 1);
  lcd.print(F("current: "));
  while (true) {
    lcd.setCursor(9, 1);
    lcd.print(brightness);
    lcd.print(F(" "));
    char key = getKeyNotBlank();
    switch (key) {
      case '+':
        if (brightness < 15)
          brightness++;
        else
          lcd.print(F("MAX"));
        break;
      case '-':
        if (brightness > 1)
          brightness--;
        else
          lcd.print(F("MIN"));
        break;
      case 'B':
        return;
    }
    lcd.print(F("   "));
    setBrightness();
  }
}

void setBrightness() {
  setBrightness(brightness * 17);
}

void setBrightness(byte number) {
  analogWrite(LED_LCD_A_PIN, number);
  PowerON = (number != 0);
}

void displayTime() {
  lcd.setCursor(0, 0);
  if (currentAlarm.hour != 24) {
    lcd.write(byte(3));
  } else lcd.print(F(" "));
  lcd.print(rtc.getTimeStr());
  lcd.print(F(" | "));
  lcd.print((byte)rtc.getTemp() - 3);
  lcd.print((char)-33);
  lcd.print(F("C"));
  lcd.setCursor(0, 1);
  lcd.print(F(" "));
  lcd.print(rtc.getDOWStr(FORMAT_SHORT));
  lcd.print(F(" "));
  lcd.print(rtc.getDateStr(FORMAT_LONG, FORMAT_LITTLEENDIAN, 165));
}

char getKeyNotBlank() {
  char key;
  while (true) {
    key = getKey();
    if (key) return key;
  }
}

char getKey() {
  //MEMORY_PRINT_FREERAM
  if (receiver.decode()) {
    //Serial.println("raw " + String(receiver.decodedIRData.decodedRawData));
    char data = decodeKey(receiver.decodedIRData.command);
    //  if (Serial.available() > 0) {
    //   char data = Serial.read();
    Serial.println(data);  // in ra Serial Monitor
    delay(50);             //hạn chế tình trạng double click!
    receiver.resume();     // nhận giá trị tiếp theo
    if (data) {
      beep(50);  // phát âm thanh 50ms để xác nhận đã nhận được tín hiệu.
      if (!PowerMode(data)) { return PowerON ? data : 0; }
    }
  }
  return 0;
}
bool PowerMode(char data) {
  if (data == 'P') {
    if (PowerON) {
      setBrightness(0);
      lcd.noDisplay();
    } else {
      setBrightness();
      lcd.display();
    }
  }
  return data == 'P';
}

const byte keyCodes[] = { 7, 8, 9, 12, 13, 21, 22, 24, 25, 28, 64, 66, 67, 68, 69, 71, 74, 82, 90, 94 };
const char keyChars[] = { '<', '4', '>', '1', 'C', 'E', '0', '2', '-', '5', '+', '7', 'B', 'T', 'P', 'M', '9', '8', '6', '3' };

char decodeKey(byte code) {
  size_t low = 0, high = sizeof(keyCodes) - 1;
  while (low <= high) {
    size_t mid = low + (high - low) / 2;
    if (code == keyCodes[mid]) {
      return keyChars[mid];
    } else if (code < keyCodes[mid]) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }
  return 0;
}

void beep(byte times, int delayTime) {
  for (byte i = 1; i < times; i++) {
    //tone(buzzer_pin, 1000, 200);
    beep();
    delay(delayTime);
  }
  beep();
}

void beep() {
  beep(200);
}

void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void testCharLCD() {
  char i = 0;
  while (true) {
    lcd.setCursor(0, 1);
    lcd.print((int)i);
    lcd.print(F(": "));
    lcd.print(i);
    lcd.print(F("       "));
    char key = getKeyNotBlank();
    if (key == '+') ++i;
    else if (key == '-') --i;
    else if (key == 'B') return;
  }
}

const byte upArrow[8] PROGMEM = { B00100, B01110, B10101, B00100, B00100, B00100, B00100, B00000 };        //mũi tên lên
const byte downArrow[8] PROGMEM = { B00100, B00100, B00100, B00100, B10101, B01110, B00100, B00000 };      // Mũi tên xuống
const byte alarmIcon[8] PROGMEM = { B00100, B01110, B01110, B01110, B11111, B00100, B00000, B00000 };      //icon báo thức
const byte sleepModeIcon[8] PROGMEM = { B01111, B00010, B00100, B01111, B11110, B00100, B01000, B11110 };  //icon ngủ

void setupLCD() {
  lcd.begin(16, 2);
  setBrightness();
  createChar(0, upArrow);
  createChar(1, downArrow);
  createChar(3, alarmIcon);
  createChar(4, sleepModeIcon);
}

void createChar(byte i, byte arr[]) {
  byte iconInRAM[8];
  memcpy_P(iconInRAM, arr, 8);
  lcd.createChar(i, iconInRAM);
}

const byte DINO_PARADO_PARTE_1[8] PROGMEM = { B00000, B00000, B00010, B00010, B00011, B00011, B00001, B00001 };
const byte DINO_PARADO_PARTE_2[8] PROGMEM = { B00111, B00111, B00111, B00100, B11100, B11100, B11000, B01000 };
const byte DINO_PIE_DERE_PART_1[8] PROGMEM = { B00000, B00000, B00010, B00010, B00011, B00011, B00001, B00001 };
const byte DINO_PIE_DERE_PART_2[8] PROGMEM = { B00111, B00111, B00111, B00100, B11100, B11100, B11000, B00000 };
const byte DINO_PIE_IZQU_PART_1[8] PROGMEM = { B00000, B00000, B00010, B00010, B00011, B00011, B00001, B00000 };
const byte DINO_PIE_IZQU_PART_2[8] PROGMEM = { B00111, B00111, B00111, B00100, B11100, B11100, B11000, B01000 };
const byte DOS_RAMAS_PART_1[8] PROGMEM = { B00000, B00100, B00100, B10100, B10100, B11100, B00100, B00100 };
const byte DOS_RAMAS_PART_2[8] PROGMEM = { B00100, B00101, B00101, B10101, B11111, B00100, B00100, B00100 };
const byte AVE_ALAS_PART1[8] PROGMEM = { B00001, B00001, B00001, B00001, B01001, B11111, B00000, B00000 };
const byte AVE_ALAS_PART2[8] PROGMEM = { B00000, B10000, B11000, B11100, B11110, B11111, B00000, B00000 };

void dinoGame(byte scored) {
  createChar(0, DINO_PARADO_PARTE_1);
  createChar(1, DINO_PARADO_PARTE_2);
  createChar(2, DINO_PIE_DERE_PART_1);
  createChar(3, DINO_PIE_DERE_PART_2);
  createChar(4, DINO_PIE_IZQU_PART_1);
  createChar(5, DINO_PIE_IZQU_PART_2);
  createChar(6, DOS_RAMAS_PART_1);
  createChar(7, DOS_RAMAS_PART_2);
  lcd.clear();

  //defino variables
  char columna_dino1 = 1;
  char columna_dino2 = 2;
  char fila_dino = 1;
  bool flag_dino = true;
  char fila_rama = 0;
  char columna_rama = 13;
  byte periodo2 = 150;
  unsigned long reloj2 = 0;
  char dino_index = 1;
  unsigned long reloj3 = 0;
  byte periodo3 = 100;
  byte puntos = 0;
  byte punto2 = 0;
  char numerorandom = 0;
  char columnaave = 13;
  bool is_jump = false;
  char aceleracion = 1;
  unsigned long last_jump = 0;

  while (true) {
    //MEMORY_PRINT_FREERAM
    if (millis() - reloj3 > periodo3) {
      reloj3 = millis();
      puntos = puntos + 1;

      if (puntos % 25 == 0 && periodo2 > 10) {
        periodo2 = periodo2 - aceleracion;  //speed
      }

      if (puntos == 100) {
        int note[] PROGMEM = { 800, 900 };
        for (byte i = 0; i < 2; i++) {
          tone(BUZZER_PIN, note[i], 150);
        }
        puntos = 10;
        if (punto2 < 100) {
          punto2++;
        }
        if (punto2 >= scored && scored > 0) {
          return;
        }
      }
      lcd.setCursor(14, 1);
      lcd.print(puntos);
      lcd.setCursor(14, 0);
      lcd.print(punto2);

      if (is_jump == false) {
        if (flag_dino) {
          lcd.setCursor(columna_dino1, fila_dino);
          lcd.write(byte(2));
          lcd.setCursor(columna_dino2, fila_dino);
          lcd.write(byte(3));
        } else {
          lcd.setCursor(columna_dino1, fila_dino);
          lcd.write(byte(4));
          lcd.setCursor(columna_dino2, fila_dino);
          lcd.write(byte(5));
        }
        flag_dino = !flag_dino;
      }
    }

    if (millis() - reloj2 > periodo2) {
      reloj2 = millis();

      columna_rama--;
      if (columna_rama < 0) {
        columna_rama = 13;
        numerorandom = random(0, 3);
        lcd.setCursor(0, 1);
        lcd.print(F(" "));

        lcd.setCursor(0, 0);
        lcd.print(F(" "));
      }
      // draw obstacles
      if (numerorandom == 1) {
        fila_rama = 1;
        createChar(6, DOS_RAMAS_PART_1);
        lcd.setCursor(columna_rama, fila_rama);
        lcd.write(byte(6));

      } else if (numerorandom == 2) {
        fila_rama = 1;
        createChar(7, DOS_RAMAS_PART_2);
        lcd.setCursor(columna_rama, fila_rama);
        lcd.write(byte(7));

      } else {
        columnaave = columna_rama;
        columnaave--;
        fila_rama = 0;
        if (columnaave > -1) {
          createChar(6, AVE_ALAS_PART1);
          lcd.setCursor(columnaave, fila_rama);
          lcd.write(byte(6));
        }
        createChar(7, AVE_ALAS_PART2);
        lcd.setCursor(columna_rama, fila_rama);
        lcd.write(byte(7));
      }
      lcd.setCursor(columna_rama + 1, fila_rama);
      lcd.print(F("  "));
    }
    //char key=getKey();
    //Serial.println("ingame "+String(key));
    if ((is_jump == false) && ((digitalRead(BUTTON) == LOW) || (receiver.decode())) && (millis() - last_jump > periodo2 * 8)) {
      last_jump = millis();
      //Serial.println("raw " + String(receiver.decodedIRData.command));
      receiver.resume();
    }
    if (millis() - last_jump < periodo2 * 7) {
      //if ((digitalRead(BUTTON) == LOW)||(columna_rama < 4 && columna_rama > -1 && fila_rama == 1)) {
      if (!is_jump) {
        dino_index = 20;
        is_jump = true;
        lcd.setCursor(columna_dino1, 0);
        lcd.write(byte(2));
        lcd.setCursor(columna_dino2, 0);
        lcd.write(byte(3));
        tone(BUZZER_PIN, 600, 150);
        lcd.setCursor(1, 1);
        lcd.print(F("  "));
      }
    } else if (is_jump) {
      dino_index = 1;
      is_jump = false;
      lcd.setCursor(1, 0);
      lcd.print(F("  "));
    }

    if (((columna_rama == dino_index || columna_rama == dino_index + 1) && fila_rama == 1)
        || (is_jump && (columna_rama == 1 || columna_rama == 2 || columnaave == 1 || columnaave == 2) && fila_rama == 0)) {  //condicion de la rama
      byte note[] PROGMEM = { 200, 150 };
      for (byte i = 0; i < 2; i++) {
        tone(BUZZER_PIN, note[i], 250);
        delay(200);
      }
      lcd.clear();
      lcd.setCursor(3, 0);
      lcd.print(F("GAME OVER"));

      lcd.setCursor(1, 1);
      lcd.print(F("Your Score:"));
      lcd.print(punto2);
      lcd.print(puntos);
      //delay(2000);
      unsigned long read_time = millis();
      while (digitalRead(BUTTON)) {
        if (receiver.decode()) {
          receiver.resume();
          break;
        }
        if (millis() - read_time > 5000) beep(50);
      }
      lcd.clear();
      columna_rama = 15;
      periodo2 = 100;
      puntos = 0;
      punto2 = 0;
    }
  }
}

void keepMoving(byte scored) {
  lcd.clear();
  int sec = scored * 60;
  unsigned long last_update = millis();
  while (true) {
    lcd.setCursor(0, 0);
    lcd.print(F(" Keep moving in "));
    lcd.setCursor(5, 1);
    lcd.print(String(sec) + F("s     "));
    if (digitalRead(RADAR_OUT_PIN) == LOW) {
      beep(50);
      sec = scored * 60;
    }
    if (millis() - last_update > 1000) {
      if (sec == 0) return;
      sec--;
      last_update = millis();
    }
  }
}
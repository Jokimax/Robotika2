#include <Arduino.h>
#include <LiquidCrystal.h>
#include <TimerOne.h>
#include <EEPROM.h>

struct __attribute__((packed)) SaveData {
    uint16_t magic;
    uint32_t totalCents;
    uint16_t itemsBought;
};

LiquidCrystal lcd(12, 11, 5, 6, 7, 8);

const uint8_t coinScanner[3] = {A0, A1, A2};
const uint16_t coinValues[3] = {100, 80, 20};
const uint8_t buyButtons[3] = {2, 3, 4};
const uint16_t prices[3] = {50, 100, 250};

const uint32_t CANT_BUY_START_TIME = 500;
const uint32_t BOUNCE_PREVENTION_TIME = 30;
const uint16_t LCD_UPDATE_TIME = 250;
const uint16_t COIN_CHECKING_TIME = 1000;

const uint16_t MAGIC_VALUE = 0xABCD;
const int EEPROM_ADDRESS = 0;

volatile uint32_t miliseconds = 0;
volatile bool updateLCD = false;
volatile bool CheckCoinsFlag = false;
volatile bool flagButton[3] = {false, false, false};

bool canBuy = false;

uint32_t cents = 0;
uint16_t itemsBought = 0;

int minVal[3];
int maxVal[3];

bool armed[3];
bool releasing[3];
uint32_t releaseStart[3];

void loadSettings();
void saveSettings();
void setupTimer();
void setupButtons();
void updateLcd();
void resetMinMax();
void readCoinPhotosensors();
void checkCoins();
void buy(int);
void handleButtons();
void printEuro(uint32_t);
void printState();
void isrButtonD2();
void isrButtonD3();

void setup() {
    Serial.begin(9600);
    lcd.begin(16, 2);

    for (int i = 0; i < 3; ++i) {
        pinMode(coinScanner[i], INPUT);
    }

    loadSettings();

    Serial.print("Boot -> ");
    printState();

    resetMinMax();
    updateLcd();
    setupButtons();
    setupTimer();
}

void loop() {
    if (!canBuy && miliseconds >= CANT_BUY_START_TIME) {
        canBuy = true;
    }

    handleButtons();
    readCoinPhotosensors();

    if (CheckCoinsFlag) {
        CheckCoinsFlag = false;
        checkCoins();
    }

    if (updateLCD) {
        updateLCD = false;
        updateLcd();
    }

    saveSettings();
}

ISR(PCINT2_vect) {
    if (!(PIND & (1 << PD4))) {
        flagButton[2] = true;
    }
}

void isrButtonD2() {
    flagButton[0] = true;
}

void isrButtonD3() {
    flagButton[1] = true;
}

void loadSettings() {
    SaveData saveData;
    EEPROM.get(EEPROM_ADDRESS, saveData);

    if (saveData.magic != MAGIC_VALUE) {
        Serial.println("EEPROM: no valid data, starting from 0");
        return;
    }

    Serial.println("EEPROM: loaded saved values");

    cents = saveData.totalCents;
    itemsBought = saveData.itemsBought;
}

void saveSettings() {
    SaveData saveData;
    saveData.magic = MAGIC_VALUE;
    saveData.totalCents = cents;
    saveData.itemsBought = itemsBought;
    EEPROM.put(EEPROM_ADDRESS, saveData);
}

void timerTick() {
    static uint16_t lcdCount = 0;
    static uint16_t secCount = 0;

    ++miliseconds;

    if (++lcdCount >= LCD_UPDATE_TIME) {
        lcdCount = 0;
        updateLCD = true;
    }

    if (++secCount >= COIN_CHECKING_TIME) {
        secCount = 0;
        CheckCoinsFlag = true;
    }
}

void setupTimer() {
    Timer1.initialize(1000);
    Timer1.attachInterrupt(timerTick);
}

void setupButtons() {
    for (int i = 0; i < 3; ++i) {
        pinMode(buyButtons[i], INPUT_PULLUP);
        armed[i] = digitalRead(buyButtons[i]) == HIGH;
        releasing[i] = false;
    }

    attachInterrupt(digitalPinToInterrupt(2), isrButtonD2, FALLING);
    attachInterrupt(digitalPinToInterrupt(3), isrButtonD3, FALLING);

    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT20);
}

void updateLcd() {
    char line[17];

    snprintf(line, sizeof(line), "%lu.%02lu EUR", static_cast<unsigned long>(cents / 100), static_cast<unsigned long>(cents % 100));
    lcd.setCursor(0, 0);
    lcd.print(line);
    for (int i = strlen(line); i < 16; ++i) lcd.print(' ');

    snprintf(line, sizeof(line), "Items bought: %u", itemsBought);
    lcd.setCursor(0, 1);
    lcd.print(line);
    for (int i = strlen(line); i < 16; ++i) lcd.print(' ');
}

void resetMinMax() {
    for (int i = 0; i < 3; ++i) {
        minVal[i] = 1023;
        maxVal[i] = 0;
    }
}

void readCoinPhotosensors() {
    for (int i = 0; i < 3; ++i) {
        int value = analogRead(coinScanner[i]);
        if (value < minVal[i]) minVal[i] = value;
        if (value > maxVal[i]) maxVal[i] = value;
    }
}

void checkCoins() {
    for (int i = 0; i < 3; ++i) {
        if ((maxVal[i] - minVal[i]) * 5 > maxVal[i]) {
            cents += coinValues[i];

            Serial.print("Coin on A");
            Serial.print(i);
            Serial.print(" (min ");
            Serial.print(minVal[i]);
            Serial.print(", max ");
            Serial.print(maxVal[i]);
            Serial.print("): +");
            printEuro(coinValues[i]);
            Serial.print(" -> ");
            printState();
        }
    }
    resetMinMax();
}

void buy(int i) {
    Serial.print("Button D");
    Serial.print(buyButtons[i]);
    Serial.print(": price ");
    printEuro(prices[i]);

    if (cents < prices[i]) {
        Serial.print(", not enough money -> ");
        printState();
        return;
    }

    cents -= prices[i];
    ++itemsBought;

    Serial.print(", bought -> ");
    printState();
}

void printEuro(uint32_t value) {
    Serial.print(value / 100);
    Serial.print('.');
    if (value % 100 < 10) Serial.print('0');
    Serial.print(value % 100);
    Serial.print(" EUR");
}

void printState() {
    Serial.print("money: ");
    printEuro(cents);
    Serial.print(", items bought: ");
    Serial.println(itemsBought);
}

void handleButtons() {
    for (int i = 0; i < 3; ++i) {
        bool pressed = digitalRead(buyButtons[i]) == LOW;

        if (!armed[i]) {
            if (pressed) {
                releasing[i] = false;
            } else if (!releasing[i]) {
                releasing[i] = true;
                releaseStart[i] = miliseconds;
            } else if (miliseconds - releaseStart[i] >= BOUNCE_PREVENTION_TIME) {
                armed[i] = true;
                releasing[i] = false;
            }
        }

        if (!flagButton[i]) continue;
        flagButton[i] = false;

        if (!armed[i] || !pressed) continue;
        armed[i] = false;

        if (canBuy) {
            buy(i);
        } else {
            Serial.print("Button D");
            Serial.print(buyButtons[i]);
            Serial.println(": ignored, startup lockout");
        }
    }
}

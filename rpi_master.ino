// --------------------------- rpi_master ----------------------------
// define the ATTINY85 pins
#define AUX_PWR_IN 3      // aux power status, 1 = on
#define AUX_PWR_OUT 1     // aux power status to RPi, 1 = on
#define MAIN_PWR_EN 4     // main power enable 1 = enable
#define PI_PWR_DOWN_REQ 2 // Request RPi power down 1 = request power down
#define PI_ALIVE 0        // Rpi alive or shutdown. Toggling = alive. Fixed 1 or 0 = shutdown

// timing constants
#define LOOP_DELAY 100 // 100mS. Assume that this is the majority of the loop execution time
#define TEN_SEC 10*1000/LOOP_DELAY
#define THIRTY_SEC 30*1000/LOOP_DELAY
#define ONE_MIN 60*1000/LOOP_DELAY
#define TEN_MIN 600*1000/LOOP_DELAY
#define PI_BOOT_TIME ONE_MIN
//#define BEFORE_SHUTDOWN_TIME TEN_MIN
#define BEFORE_SHUTDOWN_TIME TEN_SEC
#define PI_SHUTDOWN_TIME THIRTY_SEC
#define POWER_DECAY_TIME TEN_SEC

int auxPwrOnPipe;
byte atomState;
bool auxPwrOn;
bool piAlive;
int loopCnt;

// FSM States
#define WAIT_AUX_PWR_ON 0
#define WAIT_PI_ALIVE 1
#define WAIT_AUX_PWR_OFF 2
#define WAIT_BEFORE_SHUTDOWN 3
#define WAIT_PI_DEAD 4
#define WAIT_POWER_DECAY_DELAY 5

void setup() {
  // configure the pins
  pinMode(AUX_PWR_IN, INPUT);
  pinMode(AUX_PWR_OUT, OUTPUT);
  digitalWrite(AUX_PWR_OUT, LOW);
  pinMode(MAIN_PWR_EN, OUTPUT);
  digitalWrite(MAIN_PWR_EN, LOW);
  pinMode(PI_PWR_DOWN_REQ, OUTPUT);
  digitalWrite(PI_PWR_DOWN_REQ, LOW);
  pinMode(PI_ALIVE, INPUT);

  // init variables
  atomState = WAIT_AUX_PWR_ON;
  auxPwrOnPipe = 0x0;
  auxPwrOn = false;
  piAlive = false;
  loopCnt = 0;
}

void loop() {
  loopCnt++;
  
  // debounce AUX_PWR_IN, and drive AUX_PWR_OUT
  auxPwrOnPipe = auxPwrOnPipe >> 1; 
  if (digitalRead(AUX_PWR_IN) == HIGH)
    auxPwrOnPipe |= 0x80;
  else
    auxPwrOnPipe &= 0x7f;
  if (auxPwrOnPipe == 0xff) {
    auxPwrOn = true;
    digitalWrite(AUX_PWR_OUT, HIGH);
  }
  if (auxPwrOnPipe == 0x00) {
    auxPwrOn = false;
    digitalWrite(AUX_PWR_OUT, LOW);
  }
  
  // read PI_ALIVE
  if (digitalRead(PI_ALIVE) == HIGH)
    piAlive = true;
  else
    piAlive = false;

  // ----------------------- FSM ----------------
  switch (atomState) {
    // Wait for the aux power to come up, and then power up the board
    case WAIT_AUX_PWR_ON:
      if (auxPwrOn) {
        digitalWrite(MAIN_PWR_EN, HIGH);
        digitalWrite(PI_PWR_DOWN_REQ, LOW);
        atomState = WAIT_PI_ALIVE;
        loopCnt = 0;
      }
      break;
    // Wait for the RPi to report that it is alive. Give it 30s
    case WAIT_PI_ALIVE:
      if (piAlive || (loopCnt == PI_BOOT_TIME))
        atomState = WAIT_AUX_PWR_OFF;
      break;
    // Wait for the aux power to be removed
    case WAIT_AUX_PWR_OFF:
      if (!auxPwrOn) {
        atomState = WAIT_BEFORE_SHUTDOWN;
        loopCnt = 0;
      }
      break;
    // Give the RPi time to get updates from home WiFi and/or perform uC updates
    // Could extend this time so that the system is more responsive for short stops
    // ie keep the RPi active for 1 hour or more. Could even anticipate return of owner
    // and power up the RPi
    case WAIT_BEFORE_SHUTDOWN:
      // if the aux power returns then abort shutdown
      if (auxPwrOn)
        atomState = WAIT_PI_ALIVE;
      // else if we have given the RPi to take care of updates etc, then shutdown
      else if (loopCnt == BEFORE_SHUTDOWN_TIME) {
        digitalWrite(PI_PWR_DOWN_REQ, HIGH);
        atomState = WAIT_PI_DEAD;
        loopCnt = 0;
      }
      break;
    // Give the RPi 1min to shutdown and then kill the power even it is not ready
    // Also need to kill the power even if the auxPwrOn comes back up because now we 
    // do not know the state of the Rpi, so we need to power down, before we power up.
    // The RPi will need to ensure that it has completed all housekeeping well before
    // The power is killed, because it is expected to shutdown within 1min.
    case WAIT_PI_DEAD:
      if (loopCnt == PI_SHUTDOWN_TIME || !piAlive) {
        atomState = WAIT_POWER_DECAY_DELAY;
        digitalWrite(MAIN_PWR_EN, LOW);
        digitalWrite(PI_PWR_DOWN_REQ, LOW);
        loopCnt = 0;
      }
      break;
    // Allow time for the supply voltage to decay
    case WAIT_POWER_DECAY_DELAY:
      if (loopCnt == POWER_DECAY_TIME)
        atomState = WAIT_AUX_PWR_ON;
      break;
  }
  
  delay(LOOP_DELAY);
}

# Night Light Firmware

## Operating modes

1. **Emergency mode**: the LED turns on when main power goes missing and remains in this state until it's restored.
2. **Night mode**: the LED turns on (with a fading effect) if the light level is below the threshold and motion is detected, and remains in this state until the timeout expires, if another movement is detected before the timeout expires the timeout is restarted.
3. **Dual**: this is an OR of the two modes, the emergency mode has priority.
<!---4. TODO **Emergency night**: this is a partial AND of the conditions from the two modes, the LED turns on only if the main power is out AND the light level is below the threshold, regardless of motion. After turning on, the LED turns off when the main power is restored.
5. TODO **Emergency motion**: this is an AND of all the conditions from the two modes, the LED turns on only if the main power is out AND the light level is below the threshold AND motion is detected. After turning on, the LED turns off when the timeout expires or the main power is restored, if another movement is detected before the timeout expires the timeout is restarted.--->

## Settings

### Menu navigation

- User settings can be entered through a single push button and the feedback is given by LED blinks.
- The settings menu is entered with a button long press:
    - The LED blinks as many times (at a constant rate) as the index of the setting currently selected then makes a long pause.
    - The setting index is changed with a short press and wraps around when reaching the last index.
    - The currently selected setting can be edited by entering the edit mode with a long press:
        - When editing a setting, the LED blinks as many times (at a constant rate but slightly faster than before) as the currently selected value for the setting then makes a long pause.
        - The value is changed with a short press and wraps around when reaching the maximum value.
        - With a long press the menu goes back to settings navigation to allow changing other settings.
    - The menu exits when a timeout is reached (when the button is not pressed for some time).
    - **NOTE:** the setting value is applied and saved even if not returning to settings navigation before the timeout.


### Settings table

|Index  |Description                |Type       |Value mapping                          |
|-------|---------------------------|-----------|---------------------------------------|
|1      |Operating mode selection   |1 to 3     |1=emergency only 2=night only 3=dual   |
|2      |Emergency mode brigthness  |1 to 5     |1=12.5% 2=25% 3=50% 4=75% 5=100%       |
|3      |Night mode brightness      |1 to 5     |1=12.5% 2=25% 3=50% 4=75% 5=100%       |
|4      |Night mode threshold       |1 to 3     |1=0.01lx 2=0.1lx 3=1lx                 |
|5      |Night mode timeout         |1 to 3     |1=15s 2=30s 3=60s                      |

<!---|1      |Operating mode selection   |1 to 5     |1=emergency only 2=night only 3=dual 4=emergency night 5=emergency motion |--->

The default settings are always 1.

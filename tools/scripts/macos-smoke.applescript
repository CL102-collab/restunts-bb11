-- Operate the packaged startup window by PID, never by its shared game title.
on checkboxValue(controls, labelText, desiredValue)
    tell application "System Events"
        repeat with uiElement in controls
            if role of uiElement is "AXCheckBox" then
                if name of uiElement is labelText then
                    if (value of uiElement as integer) is not desiredValue then click uiElement
                    if (value of uiElement as integer) is not desiredValue then
                        error "Could not set startup checkbox: " & labelText
                    end if
                    return
                end if
            end if
        end repeat
    end tell
    error "Missing startup checkbox: " & labelText
end checkboxValue

on soundModeValue(controls, desiredValue)
    tell application "System Events"
        repeat with uiElement in controls
            if role of uiElement is "AXPopUpButton" then
                -- Tandy is also a video mode. Leave a uniquely identified sound menu closed.
                if desiredValue is not "Tandy" and (value of uiElement as text) is desiredValue then
                    if not enabled of uiElement then error "Sound mode is disabled"
                    return
                end if
            end if
        end repeat

        -- Tandy can appear in both the sound and video choices. Use the Sound mode label
        -- to find the intended popup before opening it.
        repeat with labelElement in controls
            if role of labelElement is "AXStaticText" and value of labelElement is "Sound mode" then
                set {labelX, labelY} to position of labelElement
                set {labelWidth, labelHeight} to size of labelElement
                set labelRight to labelX + labelWidth
                set labelCenterY to labelY + labelHeight / 2
                repeat with uiElement in controls
                    if role of uiElement is "AXPopUpButton" then
                        set {popupX, popupY} to position of uiElement
                        set popupHeight to item 2 of (size of uiElement)
                        if popupX >= labelRight and popupY <= labelCenterY and ¬
                            popupY + popupHeight >= labelCenterY then
                            if not enabled of uiElement then error "Sound mode is disabled"
                            if (value of uiElement as text) is not desiredValue then
                                click uiElement
                                click menu item desiredValue of menu 1 of uiElement
                                if (value of uiElement as text) is not desiredValue then
                                    error "Could not set startup sound mode: " & desiredValue
                                end if
                            end if
                            return
                        end if
                    end if
                end repeat
                exit repeat
            end if
        end repeat
    end tell
    error "Missing startup sound mode dropdown"
end soundModeValue

on startButton(controls)
    tell application "System Events"
        repeat with uiElement in controls
            if role of uiElement is "AXButton" then
                if name of uiElement is "Start game" then
                    if not enabled of uiElement then error "Start game is disabled"
                    return contents of uiElement
                end if
            end if
        end repeat
    end tell
    error "Missing Start game button"
end startButton

on run argv
    if (count of argv) is not 3 then error "Usage: ACTION LAUNCHER_PID GAME_DATA"
    set actionName to item 1 of argv
    set launcherPID to item 2 of argv as integer
    set gameData to item 3 of argv
    tell application "System Events"
        set launcherProcess to first application process whose unix id is launcherPID
        tell launcherProcess
            set frontmost to true
            set startupWindow to first window whose name is "Chocolate Stunts"
            set controls to entire contents of startupWindow
        end tell
    end tell
    if actionName is "configure" then
        my soundModeValue(controls, "Ad Lib")
        my checkboxValue(controls, "OGG music replacement (Shift+F10)", 1)
        my checkboxValue(controls, "Skip intro", 0)
        my checkboxValue(controls, "Fullscreen (Alt+Enter)", 0)
        set foundFolder to false
        tell application "System Events"
            repeat with uiElement in controls
                if role of uiElement is "AXTextField" then
                    if value of uiElement is gameData then set foundFolder to true
                end if
            end repeat
        end tell
        if not foundFolder then error "Startup window does not contain the fixture folder"
        my startButton(controls)
        return "Ad Lib selected; OGG music replacement enabled; intro enabled; windowed; fixture folder verified"
    else if actionName is "start" then
        set buttonToClick to my startButton(controls)
        tell application "System Events" to click buttonToClick
        return "Activated the startup window's Start game button"
    end if
    error "Unknown startup action: " & actionName
end run

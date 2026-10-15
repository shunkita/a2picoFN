;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                                                                                ;
; a2picoFN Configuration UI Layer (CONFIG.S)                                     ;
;                                                                                ;
; Interactive configuration screen display, key polling, and config packet       ;
; interchange with Pico / FujiNet firmware.                                      ;
;                                                                                ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;
; CONFIG HANDLER ;
;;;;;;;;;;;;;;;;;;
CFGHDLR:
        STA $C051       ; TEXT
        STA $C054       ; PAGE 1
        LDA $FBB3
        CMP #$38        ; APPLE ][
        BEQ UPPER
        CMP #$EA        ; APPLE ][+
        BEQ UPPER
        LDA $FBC0
        CMP #$EA        ; APPLE //e
        BEQ UPPER
        LDA $C01E       ; CHARSET?
        PHA
        STA $C00F       ; ALT CHARSET
        LDA $C01F       ; COLS?
        PHA
        STA $C00C       ; 40 COLS
        LDA #$F2        ; LOWERCASE
        PHP
        BNE LOOP        ; ALWAYS
UPPER:  PHP
        LDA #$F1        ; UPPERCASE

LOOP:   STA DATA
        LDA #$40        ; CONFIG
        JSR WRCMD
        BEQ QUIT_CFG    ; 0: QUIT -> Exit CONFIG
        CMP #$02
        BEQ SCAN        ; 2: SKIP -> Skip screen transfer and jump to key scan!
        CMP #$03
        BEQ ROW_UPD     ; 3: ROW UPDATE -> Update only dirty rows without HOME

        ; 1: FULL UPDATE -> Full screen redraw with HOME
SCRN:   JSR HOME        ; $FC58: Clear screen and home cursor (0, 0)
        LDX #$00        ; Row index (0..23)
LINE:   LDY #$00        ; Col index (0..39)
CHAR:   CPX #23
        BNE NOT_LAST
        CPY #39
        BNE NOT_LAST
        DEC $25         ; Decrement CV ($25) to 22 before last char to prevent vertical scroll!
NOT_LAST:
        LDA DATA
        JSR MON_COUT    ; Output character via Monitor ROM
        INY
        CPY #$28        ; 40 cols?
        BCC CHAR 
        INX
        CPX #$18        ; 24 rows?
        BCC LINE
        JMP SCAN

ROW_UPD:
        LDA DATA        ; Read number of dirty rows
        STA $02         ; Store dirty row count in scratch ZP $02
        BEQ SCAN        ; Safety: 0 rows -> jump to scan
R_LOOP: LDA DATA        ; Read row index (0..23)
        JSR MON_VTAB    ; Move cursor to (Row = A, Col = 0)
        LDY #$00        ; Col counter (0..39)
R_CHAR: LDA DATA
        JSR MON_COUT    ; Output character via Monitor ROM
        INY
        CPY #$28        ; 40 cols?
        BCC R_CHAR
        DEC $02         ; Decrement dirty row counter
        BNE R_LOOP
        ; Fall through to SCAN

SCAN:   LDA KBD
        BPL LOOP
        STA KBDSTRB
        BMI LOOP        ; ALWAYS

QUIT_CFG:
        PLP
        BEQ NOCHAR
        PLA             ; COLS
        BPL NOCOLS      ; 40 COLS?
        STA $C00D       ; 80 COLS
NOCOLS: PLA             ; CHARSET
        BMI NOCHAR      ; ALT CHARSET?
        STA $C00E       ; PRI CHARSET
NOCHAR: JSR HOME
        RTS

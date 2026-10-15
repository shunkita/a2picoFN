;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                                                                                ;
; MIT License                                                                    ;
;                                                                                ;
; Copyright (c) 2024 Oliver Schmidt (https://a2retro.de/)                        ;
;                                                                                ;
; Permission is hereby granted, free of charge, to any person obtaining a copy   ;
; of this software and associated documentation files (the "Software"), to deal  ;
; in the Software without restriction, including without limitation the rights   ;
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      ;
; copies of the Software, and to permit persons to whom the Software is          ;
; furnished to do so, subject to the following conditions:                       ;
;                                                                                ;
; The above copyright notice and this permission notice shall be included in all ;
; copies or substantial portions of the Software.                                ;
;                                                                                ;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     ;
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       ;
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    ;
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         ;
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  ;
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  ;
; SOFTWARE.                                                                      ;
;                                                                                ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                               ;
; C800 SPACE                    ;
;                               ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ORG $C800

        DFB $00         ; B/C OF RTS OFFSET

;;;;;;;;;;;;;;;;;;;
; PRODOS COMMANDS ;
;;;;;;;;;;;;;;;;;;;
PDSTS   ; EXECUTE AND GET RETURN CODE
        JSR RETCODE
        BNE PDDONE      ; ERROR?

        LDX DATA        ; SIZE LO
        LDY DATA        ; SIZE HI
PDOKAY  LDA #$00        ; SUCCESS
PDDONE  CMP #$01        ; SUCCESS?
        JMP QUIT

PDRD    ; EXECUTE AND GET RETURN CODE
        JSR RETCODE
        BNE PDDONE      ; ERROR?

        ; READ BLOCK
        JSR RDBLOCK
        JMP PDOKAY

PDWR    ; WRITE BLOCK
        JSR WRBLOCK

        ; EXECUTE AND GET RETURN CODE
        JSR RETCODE
        CMP #$01        ; SUCCESS?
        JMP QUIT

;;;;;;;;;;;;;;;;;;;;;;
; SMARTPORT COMMANDS ;
;;;;;;;;;;;;;;;;;;;;;;
STATUS  ; EXECUTE AND GET STATUS
        JSR GETSTS
        BNE RETURN      ; ERROR?

        ; READ STATUS LIST SIZE
        LDA DATA
        STA SIZEL
        LDA DATA
        STA SIZEH

        ; READ STATUS LIST
        LDY #$02        ; OFFSET OF STATUS LIST ADDRESS
        JSR SETADDR
        JSR RDBUF
        LDX SIZEL
        LDY SIZEH
        JMP RETURN

READBL  ; EXECUTE AND GET STATUS
        JSR GETSTS
        BNE RETURN      ; ERROR?

        ; READ BLOCK
        LDY #$02        ; OFFSET OF BUFFER ADDRESS
        JSR SETADDR
        JSR RDBLOCK
        JMP RETURN

WRITEBL ; WRITE BLOCK
        LDY #$02        ; OFFSET OF BUFFER ADDRESS
        JSR SETADDR
        JSR WRBLOCK
        ; FALL THROUGH

FORMAT  ; EXECUTE AND GET STATUS
        JSR GETSTS
        JMP RETURN

CONTROL ; WRITE CONTROL LIST
        LDY #$02        ; OFFSET OF CONTROL LIST ADDRSS
        JSR SETADDR
        LDY #$00        ; OFFSET OF CONTROL LIST SIZE
        JSR SETSIZE
        LDA SIZEL
        CLC
        ADC #$02        ; ADD SIZE OF CONTROL LIST SIZE
        STA SIZEL
        BCC NOINC 
        INC SIZEH
NOINC   JSR WRBUF
        ; FALL THROUGH

INIT    ; FALL THROUGH

OPEN    ; FALL THROUGH

CLOSE   ; EXECUTE AND GET STATUS
        JSR GETSTS
        JMP RETURN

READ    ; EXECUTE AND GET STATUS
        JSR GETSTS
        BNE RETURN      ; ERROR?

        ; READ BUFFER
        LDY #$04        ; OFFSET OF BYTE COUNT
        JSR SETSIZE
        LDY #$02        ; OFFSET OF BUFFER ADDRESS
        JSR SETADDR
        JSR RDBUF
        JMP RETURN

WRITE   ; WRITE BUFFER
        LDY #$04        ; OFFSET OF BYTE COUNT
        JSR SETSIZE
        LDY #$02        ; OFFSET OF BUFFER ADDRESS
        JSR SETADDR
        JSR WRBUF

        ; EXECUTE AND GET STATUS
        JSR GETSTS
        ; FALL THROUGH

RETURN  ; RESTORE ZEROPAGE LOCATIONS
        PLA
        STA ADDRL
        PLA
        STA ADDRH
        PLA
        STA SIZEL
        PLA
        STA SIZEH
        PLA             ; STATUS
        CMP #$01        ; SET CARRY
        JMP QUIT

;;;;;;;;;;;;;;;;;;
; HANDLER SWITCH ;
;;;;;;;;;;;;;;;;;;
SWITCH  BCC PRODOS
        JMP SMARTPORT

;;;;;;;;;;;;;;;;;;
; PRODOS HANDLER ;
;;;;;;;;;;;;;;;;;;
PRODOS: ; WRITE PARAMETERS
        LDX CMD
        STX DATA
        LDA UNIT
        STA DATA
        LDA ADDRL
        STA DATA
        LDA ADDRH
        STA DATA
        LDA BLOCKL
        STA DATA
        LDA BLOCKH
        STA DATA

        ; DISPATCH COMMAND
        LDA #>$C800
        PHA
        LDA PDCMD,X
        PHA
        RTS

PDCMD   DFB <PDSTS-1, <PDRD-1, <PDWR-1

;;;;;;;;;;;;;;;;;;;;;
; SMARTPORT HANDLER ;
;;;;;;;;;;;;;;;;;;;;;
SMARTPORT:
        ; SAVE ZEROPAGE LOCATIONS AND RESERVE STATUS SLOT
        TSX
        LDY #$04
SAVE    LDA ADDRL,Y
        PHA
        DEY
        BPL SAVE

        ; GET ADDRESS OF PARAMETERS AND ADJUST STACK
        CLC
        INX
        LDA $0100,X
        STA ADDRL
        ADC #<$0003     ; SIZE OF PARAMETERS
        STA $0100,X 
        INX
        LDA $0100,X
        STA ADDRH
        ADC #>$0003     ; SIZE OF PARAMETERS
        STA $0100,X

        ; PREPARE COMMAND DISPATCH
        LDA #>$C800
        PHA
        LDY #$01+$00    ; PUSH OFFSET + OFFSET OF COMMAND NUMBER
        LDA (ADDRL),Y
        TAX
        LDA SPCMD,X
        PHA

        ; WRITE COMMAND NUMBER
        STX DATA

        ; GET COMMAND LIST ADDRESS
        LDY #$01+$01    ; PUSH OFFSET + OFFSET OF COMMAND LIST ADDRESS
        JSR SETADDR

        ; WRITE COMMAND LIST
        LDA #<$0009     ; SIZE OF LARGEST COMMAND LIST
        STA SIZEL
        LDA #>$0009     ; SIZE OF LARGEST COMMAND LIST
        STA SIZEH
        JSR WRBUF

        ; DISPATCH COMMAND
        RTS

SPCMD   DFB <STATUS-1, <READBL-1, <WRITEBL-1, <FORMAT-1, <CONTROL-1
        DFB <INIT  -1, <OPEN  -1, <CLOSE  -1, <READ  -1, <WRITE  -1

.INCLUDE "CONNECT.S"


; =============================================================
; MONITOR ROUTINE WRAPPERS & SCRATCHPAD ROUTINES
; =============================================================

; -------------------------------------------------------------
; MON_INIT: Normalize text screen environment
; -------------------------------------------------------------
MON_INIT:
        BIT $C082       ; Ensure motherboard ROM read is active
        STA $C051       ; TEXT mode ON
        STA $C054       ; PAGE 1 ON
        STA $C00C       ; 40 columns ON (80 columns OFF)
        JSR SETTXT      ; $FB39: normalize text window ($20-$23 to 0,40,0,24)
        JSR SETSCR      ; $FE93: normalize CSW to COUT1
        JSR SETKBD      ; $FE89: normalize KSW to KEYIN
        JSR HOME        ; $FC58: clear screen and home cursor
        RTS

; -------------------------------------------------------------
; MON_COUT: Register-safe direct screen output via COUT1
; Input: A = Character (ASCII | $80)
; Preserves: A, X, Y
; -------------------------------------------------------------
MON_COUT:
        PHA
        TXA
        PHA
        TYA
        PHA
        BIT $C082       ; Ensure motherboard ROM read is active
        TSX
        LDA $0103,X     ; Retrieve original character from stack
        JSR COUT1       ; $FDF0: Direct screen output (bypass CSW)
        PLA
        TAY
        PLA
        TAX
        PLA
        RTS

; -------------------------------------------------------------
; MON_VTAB: Move cursor to beginning of row (CV = A, CH = 0)
; Input: A = Row index (0..23)
; Preserves: X, Y
; -------------------------------------------------------------
MON_VTAB:
        PHA
        BIT $C082       ; Ensure motherboard ROM read is active
        STA $25         ; Set CV ($25) = Row
        LDA #$00
        STA $24         ; Set CH ($24) = 0
        JSR VTAB        ; $FC22: Recalculate text line base address in BASL/BASH
        PLA
        RTS

; -------------------------------------------------------------
; PRSTR: Print NUL-terminated string
; Input: A = Pointer LO, Y = Pointer HI
; Uses scratch ZP: $02 (LO), $03 (HI)
; Preserves: X
; -------------------------------------------------------------
PRSTR:  STA $02
        STY $03
        LDY #$00
PRSTR_L LDA ($02),Y
        BEQ PRSTR_D     ; $00 (NUL) -> done
        JSR MON_COUT
        INY
        BNE PRSTR_L
PRSTR_D RTS

;;;;;;;;;;;;;;;;
; BOOT HANDLER ;
;;;;;;;;;;;;;;;;
BOOT1   LDA MSLOT
        ASL
        ASL
        ASL
        ASL
        STA $05         ; Save slot DEVSEL offset ($N0 = $10*SLOT) in scratch ZP $05
        TAY
        LDA FNSTAT,Y
        BMI FN_READY    ; Already connected -> proceed to boot immediately
        AND #$40        ; Bit 6 = 1: Unconfigured / CONFIG requested
        BNE GO_CFG      ; Jump directly to CONFIG utility

        ; Text screen initialization via Monitor ROM
        JSR MON_INIT
        LDA #<MSG_WAIT
        LDY #>MSG_WAIT
        JSR PRSTR

        ; Spinner index in scratch ZP $04
        LDA #$00
        STA $04

        ; Output initial spinner character
        TAX
        LDA SPINTBL,X
        JSR MON_COUT    ; Cursor moves to next column

W_LOOP:
        ; 1. Check FujiNet connection status ($Cn82)
        LDY $05
        LDA FNSTAT,Y
        BMI FN_READY    ; Connected! Proceed directly to boot
        AND #$40
        BNE GO_CFG      ; Unconfigured or CONFIG requested -> enter CONFIG

        ; 2. Check keyboard for manual abort ($C000)
        LDA KBD
        BPL W_DELAY     ; Bit 7 = 0 -> no key pressed
        STA KBDSTRB     ; Clear strobe
        CMP #'C'+$80
        BEQ GO_CFG
        CMP #'c'+$80
        BEQ GO_CFG
        CMP #$1B+$80    ; ESC key ($9B)
        BEQ GO_CFG
        JMP W_DELAY     ; Any other key continues waiting loop
GO_CFG: JMP DOCFG

W_DELAY:
        ; 3. Delay ~120ms using only X and Y registers
        LDX #95
DLY1    LDY #$00
DLY2    DEY
        BNE DLY2
        DEX
        BNE DLY1

        ; 4. Update spinner using DEC $24 (CH) + MON_COUT
        INC $04         ; Increment spinner index ($04)
        DEC $24         ; Move horizontal cursor CH back 1 column (over spinner)
        LDA $04
        AND #$03
        TAX
        LDA SPINTBL,X
        JSR MON_COUT    ; Overwrite spinner character (COUT1 automatically increments CH)

        ; 5. Loop indefinitely until connected or manual CONFIG entered
        JMP W_LOOP

FN_READY:
        JMP DOBOOT

MSG_WAIT:
        .BYTE 'C'|$80, 'O'|$80, 'N'|$80, 'N'|$80, 'E'|$80, 'C'|$80, 'T'|$80, 'I'|$80
        .BYTE 'N'|$80, 'G'|$80, ' '|$80, 'F'|$80, 'U'|$80, 'J'|$80, 'I'|$80, 'N'|$80
        .BYTE 'E'|$80, 'T'|$80, ' '|$80, $00

SPINTBL:
        .BYTE '/'|$80, '-'|$80, '\'|$80, '|'|$80
DOCFG   JSR CFGHDLR
        JMP BOOT1       ; Return to BOOT1 loop to wait for connection!

DOBOOT  LDX #$01
        STX CMD
        LDA MSLOT
        ASL
        ASL
        ASL
        ASL
        STA UNIT        ; DRIVE 1
        DEX
        STX ADDRL
        LDA #>$0800
        STA ADDRH
        STX BLOCKL
        STX BLOCKH
        JMP PDBOOT      ; CALL PRODOS HANDLER

BOOT2   BIT ERRTEXT     ; "SEV" -> ERROR TEXT 
        BCS DONE
        LDX $0800       ; NUMBER OF DISK II SECTORS TO READ
        DEX
        BNE DONE        ; ... MUST BE 1
        LDA $0801       ; FIRST OPCODE
        BEQ DONE        ; ... MUSTN'T BE BRK
        LDA #>($0801-1)
        PHA
        LDA #<($0801-1)
        PHA
        LDX UNIT
        JMP QUIT        ; GO FOR IT

DONE    LDA LOC0        ; POTENTIAL AUTOSTART SCAN ROM PTR LO
        BNE EXIT        ; ... MUST POINT TO A SLOT ROM START
        LDA LOC1        ; POTENTIAL AUTOSTART SCAN ROM PTR HI
        CMP MSLOT
        BNE EXIT        ; ... MUST POINT TO CURRENT SLOT ROM
        LDA #>(SLOOP-1)
        PHA
        LDA #<(SLOOP-1)
        PHA
        JMP QUIT        ; LOOKS LIKE AUTOSTART SCAN SO CONTINUE SCAN

EXIT    PHP             ; SAVE V
        JSR SETSCR
        JSR SETKBD
        JSR SETTXT
        JSR HOME
        PLP             ; RESTORE V
        BVC COLD
        LDX #$08        ; 'HDD ERROR'
ERR     LDA ERRTEXT,X
        JSR COUT
        DEX
        BPL ERR
COLD    LDA #>(BASIC-1)
        PHA
        LDA #<(BASIC-1)
        PHA
        JMP QUIT

ERRTEXT DFB 'R'|$80, 'O'|$80, 'R'|$80, 'R'|$80, 'E'|$80, ' '|$80, 'D'|$80, 'D'|$80, 'H'|$80

.INCLUDE "CONFIG.S"


        .RES $CF00-*

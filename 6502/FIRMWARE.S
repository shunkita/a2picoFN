.FEATURE    labels_without_colons
.MACPACK    apple2
.DEFINE ASC scrcode
.DEFINE EQU =
.DEFINE DFB .byte
.DEFINE ORG .org

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                               ;
; a2picoFN FIRMWARE TOP-LEVEL   ;
;                               ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;
; ZERO PAGE EQUS ;
;;;;;;;;;;;;;;;;;;
LOC0    EQU $00
LOC1    EQU $01
CMD     EQU $42         ; PRODOS COMMAND
UNIT    EQU $43         ; PRODOS UNIT
ADDRL   EQU $44         ; PRODOS / SMARTPORT BUFFER ADDRESS
ADDRH   EQU $45
BLOCKL  EQU $46         ; PRODOS BLOCK
BLOCKH  EQU $47
SIZEL   EQU $46         ; SMARTPORT BUFFER SIZE
SIZEH   EQU $47

;;;;;;;;;;;;;;;;;;;
; GENERAL EQUATES ;
;;;;;;;;;;;;;;;;;;;
KBD     EQU $C000       ; KEYBOARD INPUT
KBDSTRB EQU $C010       ; KEYBOARD CLEAR
ROMSOFF EQU $CFFF       ; DISABLES C0-RES. $C800 ROMS

;;;;;;;;;;;;;;;;;;;;;;;
; SMARTPORT ADDRESSES ;
;;;;;;;;;;;;;;;;;;;;;;;
DATA    EQU $CFF0       ; SMARTPORT DATA REGISTER
CTRL    EQU $CFF1       ; SMARTPORT CONTROL REGISTER
FNSTAT  EQU $C082       ; (+$N0) SMARTPORT / FUJINET STATUS REGISTER ($80 = READY)

;;;;;;;;;;;;;;;;;;;;;
; BANKING ADDRESSES ;
;;;;;;;;;;;;;;;;;;;;;
BANKCLR EQU $CFFB       ; CLEAR EXPANSION ROM BANK
BANKSET EQU $CFFC       ; SET EXPANSION ROM BANK

;;;;;;;;;;;;;;;;;;;;;;;
; MONITOR SUBROUTINES ;
;;;;;;;;;;;;;;;;;;;;;;;
MSLOT   EQU $7F8        ; BUFFER FOR HI SLOT ADDR (SCN)
SLOOP   EQU $FABA       ; CONTINUE SLOT SCAN
SETTXT  EQU $FB39       ; SET TEXT MODE
VTAB    EQU $FC22       ; SET CURSOR VERTICAL POS FROM CV ($25)
HOME    EQU $FC58       ; CLEAR SCREEN AND HOME CURSOR
COUT    EQU $FDED       ; CHARACTER OUT (THRU CSW)
COUT1   EQU $FDF0       ; DIRECT CHARACTER OUT TO SCREEN (BYPASS CSW)
SETKBD  EQU $FE89       ; SETS KSW TO APPLE KEYBOARD
SETSCR  EQU $FE93       ; SETS CSW TO APPLE SCREEN
BASIC   EQU $E000       ; BASIC INTERPRETER COLD START

;;;;;;;;;;;;;;;;;;;;;;;
; SLOT ROM SPACE      ;
;;;;;;;;;;;;;;;;;;;;;;;
.REPEAT 8,_SLOT
    .DEFINE SLOT _SLOT

    .IF SLOT
        .SCOPE
    .ENDIF
        .INCLUDE "SSC.CN00.S"
    .IF SLOT
        .ENDSCOPE
    .ENDIF

    .UNDEF SLOT
.ENDREP

;;;;;;;;;;
; BANK 0 ;
;;;;;;;;;;
.RES $CF00-$C800
.INCLUDE "SSC.CF00.S"

;;;;;;;;;;
; BANK 1 ;
;;;;;;;;;;
.INCLUDE "SMARTPORT.S"

.SCOPE
.INCLUDE "SSC.CF00.S"
.ENDSCOPE

;;;;;;;;;;
; BANK 2 ;
;;;;;;;;;;
.RES $CF00-$C800

.SCOPE
.INCLUDE "SSC.CF00.S"
.ENDSCOPE

;;;;;;;;;;
; BANK 3 ;
;;;;;;;;;;
.RES $CF00-$C800

.SCOPE
.INCLUDE "SSC.CF00.S"
.ENDSCOPE

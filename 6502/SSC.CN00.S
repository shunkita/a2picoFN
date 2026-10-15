;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                               ;
; APPLE II SSC FIRMWARE         ;
;                               ;
;   BY LARRY KENYON             ;
;                               ;
;    -JANUARY 1981-             ;;;;;;;;;;;;;;
;                                            ;
; (C) COPYRIGHT 1981 BY APPLE COMPUTER, INC. ;
;                                            ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                               ;
; CN00 SPACE CODE               ;
;                               ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ORG $C000+$0100*SLOT

        CMP #$20        ; $CN01:$20
        CMP #$00        ; $CN03:$00
        CMP #$03        ; $CN05:$03
        CMP #$3C        ; $CN07:$3C, WILL BE REPLACED WITH $00
        CLV
        BVC DENTRY      ; <ALWAYS> SKIP AROUND PASCAL 1.1 ENTRY

.ASSERT .LOBYTE(*)=$0B,ERROR

        DFB $01         ; GENERIC SIGNATURE BYTE
        DFB $31         ; DEVICE SIGNATURE BYTE
        DFB <PINIT
        DFB <PREAD
        DFB <PWRITE
        DFB <PSTATUS

;;;;;;;;;;;;;;;;;;;
;                 ;
;  BASIC ENTRIES  ;
;                 ;
;;;;;;;;;;;;;;;;;;;

IENTRY  RTS             ; BASIC INPUT ENTRY
OENTRY  RTS             ; BASIC OUTPUT ENTRY
BENTRY  RTS

;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                          ;
;  DISK INTERFACE ENTRIES  ;
;                          ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;

DISK    CLC             ; PRODOS DRIVER ENTRY
        BCC SETUP
        SEC             ; SMARTPORT DRIVER ENTRY
SETUP   JSR DSETUP
        JMP COMMON

DENTRY  JSR DSETUP
        JMP BOOT

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                                ;
;  NEW PASCAL INTERFACE ENTRIES  ;
;                                ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
PINIT   RTS
PREAD   RTS
PWRITE  RTS
PSTATUS RTS

;;;;;;;;;;;;;;;;;;;;;;;;
; SHARED SETUP ROUTINE ;
;;;;;;;;;;;;;;;;;;;;;;;;
DSETUP  LDX #$C0+SLOT   ; X-REG WILL GENERALLY BE $CN
        STX MSLOT
        STA ROMSOFF     ; SWITCH OUT OTHER $C800 ROMS
        RTS

        .RES $0100-$10-<*

;;;;;;;;;;;;;;;;;;
;                ;
;  CONFIG ENTRY  ;
;                ;
;;;;;;;;;;;;;;;;;;

CONFIG  JSR DSETUP
        JMP CFG

        ASC "OlSc"

        DFB $00
        DFB $00         ; SMARTPORT ID
        DFB $00,$00     ; TOTAL BLOCKS
        DFB $D7         ; STATUS BYTE

.ASSERT .LOBYTE(*)=$FF,ERROR

        DFB <DISK

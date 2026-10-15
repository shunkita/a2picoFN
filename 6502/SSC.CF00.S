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
; CF00 SPACE                    ;
;                               ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ORG $CF00

;;;;;;;;
; BOOT ;
;;;;;;;;
BOOT    LDA #$01        ; SMARTPORT FIRMWARE
        STA BANKSET
        JMP BOOT1

PDBOOT  JSR PRODOS
        LDA #$01        ; SMARTPORT FIRMWARE
        STA BANKSET
        JMP BOOT2

;;;;;;;;;;;;;;;;;;
; COMMON HANDLER ;
;;;;;;;;;;;;;;;;;;
COMMON  LDA #$01        ; SMARTPORT FIRMWARE
        STA BANKSET
        JMP SWITCH

;;;;;;;;;;;;;;;
; COMMON QUIT ;
;;;;;;;;;;;;;;;
QUIT    STA BANKCLR
        RTS

;;;;;;;;;;;;;;;;;;;;;;;
; READ 512-BYTE BLOCK ;
;;;;;;;;;;;;;;;;;;;;;;;

RDBLOCK LDA #$02        ; READ 1. PAGE CODE
        STA BANKSET
        JSR $C800
        LDA #$03        ; READ 2. PAGE CODE
        STA BANKSET
        JSR $C800
        LDA #$01        ; SMARTPORT FIRMWARE
        STA BANKSET
        RTS

;;;;;;;;;;
; CONFIG ;
;;;;;;;;;;
CFG     LDA #$01        ; SMARTPORT FIRMWARE
        STA BANKSET
        JSR CFGHDLR
        STA BANKCLR
        RTS

        .RES $D000-*

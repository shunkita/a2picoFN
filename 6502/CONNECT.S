;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;                                                                                ;
; a2picoFN Hardware Communication Layer (CONNECT.S)                              ;
;                                                                                ;
; Low-level communication routines for interfacing with RP2040/RP2350 Pico        ;
; and FujiNet via memory-mapped I/O registers:                                   ;
;   DATA: $CFF0 (Data Register)                                                  ;
;   CTRL: $CFF1 (Control / Status Register)                                      ;
;                                                                                ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;
; UTILS ;
;;;;;;;;;
SETSIZE:; SET 16-BIT BUFFER SIZE
        LDA (ADDRL),Y
        STA SIZEL
        INY
        LDA (ADDRL),Y
        STA SIZEH
        RTS

SETADDR:; SET BUFFER ADDRESS
        LDA (ADDRL),Y
        TAX
        INY
        LDA (ADDRL),Y
        STA ADDRH
        STX ADDRL
        RTS

GETSTS: ; EXECUTE SMARTPORT COMMAND AND GET STATUS
        LDA #$02        ; SMARTPORT
        JSR WRCMD

        ; SAVE STATUS IN STACK SLOT
        TSX
        TAY
        TXA
        CLC
        ADC #$07        ; OFFSET OF STACK SLOT
        TAX
        TYA
        STA $0100,X
        RTS

RETCODE:; EXECUTE PRODOS COMMAND AND GET RETURN CODE
        LDA #$01        ; PRODOS
        ; FALL THROUGH

WRCMD:  ; WRITE COMMAND REG AND READ RESPONSE
        STA CTRL
WAIT:   LDA CTRL
        BPL WAIT
        LDA DATA
        RTS

RDBUF:  ; READ BUFFER
        LDY #$00
        LDX SIZEH
        BEQ RDREST
RDFULL: JSR RDPAGE
        INC ADDRH
        DEX
        BNE RDFULL
RDREST: LDX SIZEL
        BEQ RDDONE
RDMORE: LDA DATA
        STA (ADDRL),Y
        INY
        DEX
        BNE RDMORE
RDDONE: RTS

WRBUF:  ; WRITE BUFFER
        LDY #$00
        LDX SIZEH
        BEQ WRREST
WRFULL: JSR WRPAGE
        INC ADDRH
        DEX
        BNE WRFULL
WRREST: LDX SIZEL
        BEQ WRDONE
WRMORE: LDA (ADDRL),Y
        STA DATA
        INY
        DEX
        BNE WRMORE
WRDONE: RTS

.IF 0
RDBLOCK:; READ 512-BYTE BLOCK
        LDY #$00
        JSR RDPAGE
        INC ADDRH
.ENDIF
RDPAGE: LDA DATA
        STA (ADDRL),Y
        INY
        BNE RDPAGE
        RTS

WRBLOCK:; WRITE 512-BYTE BLOCK
        LDY #$00
        JSR WRPAGE
        INC ADDRH
WRPAGE: LDA (ADDRL),Y
        STA DATA
        INY
        BNE WRPAGE
        RTS

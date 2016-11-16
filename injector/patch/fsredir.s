.nds
.create "fsredir.bin", 0

.macro addr, reg, func
    add reg, pc, #func-.-8
.endmacro
.macro load, reg, func
    ldr reg, [pc, #func-.-8]
.endmacro
.macro svc, svcnum
    .word 0xef000000 + svcnum
.endmacro

.arm
_start:

    ; Return to fsOpenFileDirectly function
    retfsOpenFileDirectly:
        load r12, fsOpenFileDirectly
        nop     ; Will be replaced with the substituted function opcode
        bx r12
    
    ; Return to iFileOpen function 
    retiFileOpen:
        ldmfd sp!, {r0-r4}
        load r12, iFileOpen
        nop     ; Will be replaced with the substituted function opcode
        bx r12 

    ; Redirect ROMFS image reading. Code flows here from fsOpenFileDirectly.
    openRomFsImage:
        cmp r3, #3
        bne retfsOpenFileDirectly
        ; We redirect ROMFS file opening by changing the parameters and call
        ; the fsOpenFileDirectly function recursively. The parameter format:
        ; r0          : fsUserHandle
        ; r1          : Output FileHandle
        ; r2          : Transaction (usually 0)
        ; r3          : Archive ID
        ; [sp, #0x00] : Archive PathType
        ; [sp, #0x04] : Archive DataPointer
        ; [sp, #0x08] : Archive PathSize
        ; [sp, #0x0C] : File PathType
        ; [sp, #0x10] : File DataPointer
        ; [sp, #0x14] : File PathSize
        ; [sp, #0x18] : File OpenFlags
        ; [sp, #0x1C] : Attributes (usually 0)
        sub sp, sp, #0x50
        stmfd sp!, {r0, r1, lr}
        add sp, sp, #0x5C
        str r3, [sp, #0x0C]   ; File PathType (ASCII = 3)
        load r12, customDataPath
        add r12, r12, #5      ; (skip 'data:')
        str r12, [sp, #0x10]  ; File DataPointer
        load r12, customDataPathSize
        str r12, [sp, #0x14]  ; File PathSize
        mov r3, #9            ; SDMC Archive ID
        bl openRomFsImage     ; Recursively call fsOpenFileDirectly
        sub sp, sp, #0x5C
        ldmfd sp!, {r0, r1, lr}
        add sp, sp, #0x50      
        ; Once we have the sd romfs file opened, we use fsOpenSubFile
        ; in order to skip the useless data.
        mov r0, r1            ; fsFileHandle
        stmfd sp!, {r1, r3-r11}
        mrc p15, 0, r4, c13, c0, 3
        add r4, r4, #0x80
        mov r1, r4
        addr r3, fsOpenSubFileCmd
        ldmia r3!, {r5-r9}
        stmia r1!, {r5-r9}
        ldr r0, [r0]
        svc 0x32
        ldr r0, [r4, #0x0C]
        ldmfd sp!, {r1, r3-r11}
        str r0, [r1]
        mov r0, #0
        bx lr

    ; Redirect SaveData file path. Code flows here from iFileOpen.
    saveFileRedirect:
        stmfd sp!, {r0-r4}
        load r4, customDataPath
        ldrb r4, [r4]
        ldrb r3, [r1]
        cmp r3, r4
        bne retiFileOpen
        sub r0, sp, #0x200
        mov r4, r0
        load r3, customDataPath
        saveDataPathCopy1:
            ldrb r2, [r3], #1
            strh r2, [r0], #2
            cmp r2, #0x72  ; 'r'
            bne saveDataPathCopy1
        add r1, r1, #0xA
        sub r0, r0, #4
        saveDataPathCopy2:
            ldrh r2, [r1], #2
            strh r2, [r0], #2
            cmp r2, #0
            bne saveDataPathCopy2
        str r4, [sp, #4]
        b retiFileOpen

.pool
.align 4
    ; Functions and pointers
    fsOpenFileDirectly       : .word 0x00000000 ; fsOpenFileDirectly + 4
    iFileOpen                : .word 0x00000000 ; iFileOpen + 4
    customDataPath           : .word 0x00000000 ; "data:/luma/titles/<titleid>/romfs"
    customDataPathSize       : .word 0x00000000 ; sizeof("/luma/titles/<titleid>/romfs")
    fsOpenSubFileCmd         : .word 0x08010100
                               .word 0x00000000 ; RomFS File Offset
                               .word 0x00000000 
                               .word 0x00000000 ; RomFS File Size
                               .word 0x00000000
.close

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#define OUT_OUTLINE_PRECISION 8
#define CLIP_DEFAULT_PRECISION 0
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// --- КОНСТАНТЫ АРХИТЕКТУРЫ Z80 АОН (СТРОГО ПО ПРИНЦИПИАЛЬНОЙ СХЕМЕ) ---
const DWORD ROM_SIZE = 65536; // Физическое адресное пространство под ПЗУ (до 64 КБ)
const DWORD RAM_SIZE = 8192;  // Физический размер микросхемы ОЗУ РУ17 (строго 8 КБ)
BYTE rom[ROM_SIZE];           // Сюда загружается прошивка
BYTE ram[RAM_SIZE];           // Физические 8 КБ оперативной памяти
int rom_size = 2048;

long loaded_rom_file_size = 2048; //
wchar_t last_read_key_char = L'-'; //
char display_digits[9] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }; //
char display_latch[9] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' , ' ' }; //
int digit_lifetime[9] = { 0 }; //
const int MAX_LIFETIME = 25;   // Скорректировано для плавного послесвечения

int active_digit = 0;              // Текущий выбранный разряд дисплея (AON.select)
BYTE ir23_display_segments = 0x00; // Хранит текущую маску сегментов, записанную в ИР23
bool keys_pressed[12] = { false }; // Оставляем для связи с 12 GUI кнопками

// --- ПОЛНОСТЬЮ ИСПРАВЛЕННАЯ СТРУКТУРА МАТРИЦЫ КЛАВИАТУРЫ РУСЬ-9 ---
// Пересчитана на основе эмпирической карты откликов.
// Средний вертикальный ряд переведен на линию KD3 {3}, чтобы обойти блокировку шины.
struct KeyMatrixMap {
    BYTE port_b_bit; // 0..3 (Линии KD0..KD3 на Порту B)
    BYTE id4_row;    // 0..7 (Выходы дешифратора DD9 ИД4)
} key_matrix_layout[12] = {
    {0, 2}, {2, 2}, {3, 2}, // '1', '2', '3'
    {0, 3}, {2, 3}, {3, 3}, // '4', '5', '6'
    {0, 0}, {2, 0}, {3, 0}, // '7', '8', '9'
    {0, 1}, {2, 1}, {3, 1}  // '*', '0', '#'
};

bool emulator_running = true; //

// --- ЗВУКОВАЯ СИСТЕМА И КОЛЬЦЕВОЙ БУФЕР ---
bool rk_inte_speaker_state = false; //
bool rk_port_c_speaker_state = false; //
bool vi53_out0_state = false; //
const int AUDIO_SAMPLE_RATE = 22050; //
HWAVEOUT hWaveOut = NULL; //
WAVEHDR waveHeader[2]; //
short* audioBuffers[2] = { NULL, NULL }; //
const int AUDIO_BUF_SIZE = 1024; //
const int INTE_RING_BUF_SIZE = 16384; //
short inte_ring_buffer[INTE_RING_BUF_SIZE] = { 0 }; //
volatile int inte_ring_write_ptr = 0; //
volatile int inte_ring_read_ptr = 0; //

HWND hEditDisplay = NULL; //
HWND hEditDebug = NULL; //
HWND hEditRAM = NULL; //
HWND hEditRAM_Low = NULL; //
HWND hBtnKeys[12] = { NULL }; //
UINT_PTR timerId = 0; //
WNDPROC OldButtonProc = NULL; //

// Прямое декодирование сегментов индикатора HG1 БЕЗ ложной инверсии
char DecodeSegment(BYTE segments) {
    // Выделяем чистые сегменты без учета десятичной точки
    BYTE pure_segments = segments & 0x7F;
    char character = ' ';

    switch (pure_segments) {
    case 0x3F: character = '0'; break;
    case 0x06: character = '1'; break;
    case 0x5B: character = '2'; break;
    case 0x4F: character = '3'; break;
    case 0x66: character = '4'; break;
    case 0x6D: character = '5'; break;
    case 0x7D: character = '6'; break;
    case 0x07: character = '7'; break;
    case 0x7F: character = '8'; break;
    case 0x6F: character = '9'; break;
    case 0x40: character = '-'; break;
    case 0x50: character = 'r'; break;
    case 0x3E: character = 'U'; break;
    case 0x79: character = 'E'; break;
    case 0x71: character = 'F'; break;
    case 0x76: character = 'H'; break;
    case 0x38: character = 'L'; break;
    case 0x73: character = 'P'; break;
    case 0x78: character = 't'; break;
    case 0x6E: character = 'Y'; break;
    case 0x00: character = ' '; break;
    case 0x77: character = 'A'; break;
    case 0x7C: character = 'b'; break;
    case 0x39: character = 'C'; break;
    case 0x58: character = 'c'; break;
    case 0x5E: character = 'd'; break;
    case 0x3D: character = 'G'; break;
    case 0x74: character = 'h'; break;
    case 0x1E: character = 'J'; break;
    case 0x54: character = 'n'; break;
    case 0x5C: character = 'o'; break;
    case 0x1C: character = 'u'; break;
    case 0x08: character = '_'; break;
    case 0x0F: character = ')'; break;
    default: character = '?';
    }

    // Если горит старший бит (сегмент H/Dp) — возвращаем точку
    if (segments & 0x80) {
        if (character == ' ') return '.';
    }
    return character;
}

struct PIT8253 {
    struct Channel {
        WORD count_register;   // Регистр предустановки счетчика
        WORD latch_register;   // Регистр защелки данных
        int count_down;       // Текущее значение бегущего счетчика
        BYTE mode;             // Режим работы (0..5)
        BYTE read_write_state; // Триггер фазы доступа LSB/MSB (0 или 1)
        BYTE access_mode;      // Режим доступа (1-LSB, 2-MSB, 3-BOTH)
        bool latched;          // Флаг фиксации данных в защелке
        bool out_signal;       // Физический уровень на выходе OUT канала
        bool gate;              // новое поле — состояние входа GATE канала
    } channels[3];

    void Reset() {
        for (int i = 0; i < 3; i++) {
            channels[i].count_register = 0xFFFF; // Инициализируем максимальным периодом Intel
            channels[i].latch_register = 0;
            channels[i].count_down = 0xFFFF;
            channels[i].mode = 1;                // По умолчанию Mode 1 строго по Borik
            channels[i].read_write_state = 0;
            channels[i].access_mode = 3;         // Доступ BOTH
            channels[i].latched = false;
            channels[i].out_signal = true;
            channels[i].gate = true;   // безопасное начальное состояние
        }
    }

    // Вспомогательная функция: трактует 0 как 65536 (0x10000)
    int GetCountValue(WORD val) {
        return (val == 0) ? 65536 : (int)val;
    }

    void WriteControl(BYTE val) {
         int ch = (val >> 6) & 0x03;
         if (ch == 3) return; // Read-back не поддерживается в Intel 8253

         int access = (val >> 4) & 0x03;
         int m = (val >> 1) & 0x07;
         if (m > 5) m -= 4; // Коррекция для дублирующих масок режимов 2 и 3

         if (access == 0) {
             // Команда защелкивания счета (Counter Latch Command)
             channels[ch].latch_register = channels[ch].count_down;
             channels[ch].latched = true;
         }
         else {
             channels[ch].access_mode = access;
             channels[ch].mode = m;
             channels[ch].read_write_state = 0;
             channels[ch].latched = false;
             // Инициализация выхода OUT согласно режиму:
             // Mode 0: OUT = 0 (low)
             // Mode 1: OUT = 1 (high)
             // Mode 2: OUT = 1 (high)
             // Mode 3: OUT = 1 (high)
             // Mode 4: OUT = 1 (high)
             // Mode 5: OUT = 1 (high)
             channels[ch].out_signal = (m != 0);
         }
    }

    void WriteData(int ch, BYTE val) {
         Channel& c = channels[ch];
         if (c.access_mode == 1) { // Только LSB
             c.count_register = val;
             c.count_down = GetCountValue(c.count_register);
         }
         else if (c.access_mode == 2) { // Только MSB
             c.count_register = (val << 8);
             c.count_down = GetCountValue(c.count_register);
         }
         else if (c.access_mode == 3) { // Оба байта (BOTH)
             if (c.read_write_state == 0) {
                 c.count_register = (c.count_register & 0xFF00) | val;
                 c.read_write_state = 1;
             }
             else {
                 c.count_register = (c.count_register & 0x00FF) | (val << 8);
                 c.count_down = GetCountValue(c.count_register);
                 c.read_write_state = 0;
                 // На момент полной загрузки константы взводим выход OUT согласно режиму
                 c.out_signal = (c.mode == 0 || c.mode == 1) ? false : true;
             }
         }
    }

    BYTE ReadData(int ch) {
        Channel& c = channels[ch];
        BYTE ret = 0;
        WORD target = c.latched ? c.latch_register : c.count_down;

        if (c.access_mode == 1) {
            ret = target & 0xFF;
            c.latched = false;
        }
        else if (c.access_mode == 2) {
            ret = (target >> 8) & 0xFF;
            c.latched = false;
        }
        else if (c.access_mode == 3) {
            if (c.read_write_state == 0) {
                ret = target & 0xFF;
                c.read_write_state = 1;
            }
            else {
                ret = (target >> 8) & 0xFF;
                c.read_write_state = 0;
                c.latched = false;
            }
        }
        return ret;
    }

    // --- ПОЛНОСТЬЮ ИСПРАВЛЕННЫЙ МЕТОД ОБНОВЛЕНИЯ ТАЙМЕРА ---
    void Update(int cycles) {
        for (int i = 0; i < 3; i++) {
            Channel& c = channels[i];
            if (c.access_mode == 0) continue;
            if (!c.gate) continue;     // как в эталоне: без GATE счёт не идёт

            // Потактовая эмуляция для корректной работы всех режимов
            for (int t = 0; t < cycles; t++) {
                switch (c.mode) {
                case 0:  // Mode 0: Interrupt on Terminal Count
                    // Линейный декремент счетчика, OUT переходит в HIGH когда count==0
                    if (c.count_down > 0) {
                        c.count_down--;
                    }
                    if (c.count_down == 0) {
                        c.out_signal = true;
                    }
                    break;

                case 1:  // Mode 1: Hardware Retriggerable One-Shot
                    // Одноразовый импульс (упрощённая реализация - линейный декремент)
                    if (c.count_down > 0) {
                        c.count_down--;
                    }
                    if (c.count_down == 0) {
                        c.out_signal = true;  // по терминальному счёту OUT возвращается в HIGH
                    }
                    break;

                case 2:  // Mode 2: Rate Generator (Tone Generator)
                    // Частотный генератор: импульс с частотой f/N (N = count_register)
                    // OUT остается HIGH, падает на 1 такт перед перезагрузкой
                    if (c.count_down > 0) {
                        c.count_down--;
                    }
                    if (c.count_down == 0) {
                        // Перезагружаем счетчик
                        int reload = GetCountValue(c.count_register);
                        c.count_down = reload;
                        c.out_signal = true;  // OUT поднимается после импульса
                    }
                    else if (c.count_down == 1) {
                        // На последнем такте перед обнулением OUT падает
                        c.out_signal = false;
                    }
                    break;

                case 3:  // Mode 3: Square Wave Generator (Meander)
                    if (c.count_down > 0) {
                        // Уменьшаем счетчик на 2 такта за шаг, так как меандр считает парами
                        c.count_down -= 2;
                    }
                    if (c.count_down <= 0) {
                        // Переключаем полярность полупериода звука/времени
                        c.out_signal = !c.out_signal;

                        int reload = GetCountValue(c.count_register);
                        // Загружаем новый полупериод
                        c.count_down = reload;
                    }
                    break;

                case 4:  // Mode 4: Software Triggered Strobe
                case 5:  // Mode 5: Hardware Triggered Strobe (GATE не моделируется, упрощённо как программный)
                    if (c.count_down > 0) {
                        c.count_down--;
                        if (c.count_down == 1) {
                            c.out_signal = false; // однотактный строб-импульс перед нулём
                        }
                    }
                    if (c.count_down == 0) {
                        c.out_signal = true;      // импульс окончен, счётчик останавливается (без перезагрузки)
                    }
                    break;
                }
            }
        }
    }
} pit;

#pragma pack(push, 1)
struct CPUZ80 {
    // Основные регистры (строго 8-бит)
    BYTE A, B, C, D, E, H, L;
    BYTE F; // Флаги: S(7) Z(6) X(5) H(4) X(3) P/V(2) N(1) C(0)

    // Альтернативные регистры (строго 8-бит shadow set)
    BYTE A_alt, F_alt, B_alt, C_alt, D_alt, E_alt, H_alt, L_alt;

    // Системные регистры (16-бит и 8-бит)
    WORD PC, SP;
    WORD IX, IY;
    BYTE I, R;
    // IM0 support helper: byte placed on data bus by external device when IM=0
    BYTE interrupt_vector_bus_byte = 0x00;
    bool IFF1, IFF2; // Флаги разрешения прерываний Z80
    BYTE IM;         // Режим прерываний (0, 1 или 2)
    bool halted;
    bool int_pending;
    int ei_delay_counter = 0; // counter to implement EI one-instruction delay (2 -> will enable after next step)

    BYTE ppi_pa, ppi_pb, ppi_pc, ppi_ctrl; //

    void Reset() {
        PC = 0x0000;
        SP = 0x3FFF;
        A = 0; F = 0; B = 0; C = 0; D = 0; E = 0; H = 0; L = 0;
        IX = 0; IY = 0; IFF1 = IFF2 = false; 
        IM = 0; 
        halted = false;
        int_pending = false;
        ei_delay_counter = 0;

        // Аппаратный /RES сбрасывает и КР580ВВ55А — обязательно возвращаем
        // выбор банка ПЗУ (PA6) и остальные защёлки портов в исходное состояние,
        // иначе повторный Reset() с 64 КБ ПЗУ прочитает вектор сброса из "зависшего" банка.
        ppi_pa = 0;
        ppi_pb = 0;
        ppi_pc = 0;
        ppi_ctrl = 0;

        // Очищаем ОЗУ
        for (int i = 0; i < 8192; ++i) {
            ram[i] = rand() & 255;
        }
    }

    // --- ПАССИВНАЯ ФУНКЦИЯ ОБНОВЛЕНИЯ ДИСПЛЕЯ И ТРУБКИ ---
    void UpdateHardwareState() {
        // Вычисляем активный разряд дисплея строго по схеме АОН Русь
        if (ppi_pa & 0x08) {
            active_digit = 0;
        }
        else {
            active_digit = (ppi_pa & 0x07) + 1;
        }

        int display_pos = active_digit;
        char current_char = DecodeSegment(ir23_display_segments);

        if (display_pos >= 0 && display_pos < 9) {
            if (ir23_display_segments != 0x00 && current_char != ' ') {
                for (int d = 0; d < 8; ++d) {
                    if (d != display_pos) {
                        if (digit_lifetime[d] > 5) {
                            digit_lifetime[d] = 5;
                        }
                    }
                }
                display_latch[display_pos] = current_char;
                display_digits[display_pos] = current_char;
                digit_lifetime[display_pos] = MAX_LIFETIME;
            }
        }

        // Синхронизируем состояние телефонной трубки (Линия С1 / PB2)
        HWND hParent = hEditDisplay ? GetParent(hEditDisplay) : NULL;
        HWND hCheck = hParent ? GetDlgItem(hParent, 200) : NULL;

        if (hCheck) {
            LRESULT checkState = SendMessageW(hCheck, BM_GETCHECK, 0, 0);
            if (checkState == BST_CHECKED) {
                ppi_pb &= ~0x04;   // Трубка ПОДНЯТА -> PB2 = 0
            }
            else {
                ppi_pb |= 0x04;    // Трубка ОПУЩЕНА -> PB2 = 1
            }
        }

        // Фиксируем символ для отладочной панели GUI
        for (int i = 0; i < 12; i++) {
            if (keys_pressed[i]) {
                const wchar_t* layout = L"123456789*0#";
                last_read_key_char = (char)layout[i];
                break;
            }
        }
    }

    BYTE ReadMem(WORD address) {
        if (address & 0x2000)
        {
            return ram[address & (RAM_SIZE - 1)];
        }
        else
        {
            address = (address & 0x1fff) | ((address >> 1) & 0xe000);	//translate ROM address lines
            if (ppi_pa & 0x40) 
                address |= 0x8000;	//second 32K bank
            return rom[address & (rom_size - 1)];
        }
    }

    void WriteMem(WORD address, BYTE value) {
        if (address & 0x2000) ram[address & (RAM_SIZE - 1)] = value;
    }

    // ОБНОВЛЕНО: Безопасные и изолированные функции регистровых пар Z80
    WORD GetBC() { return ((WORD)B << 8) | C; }
    void SetBC(WORD val) { B = (BYTE)(val >> 8); C = (BYTE)(val & 0xFF); }

    WORD GetDE() { return ((WORD)D << 8) | E; }
    void SetDE(WORD val) { D = (BYTE)(val >> 8); E = (BYTE)(val & 0xFF); }

    WORD GetHL() { return ((WORD)H << 8) | L; }
    void SetHL(WORD val) { H = (BYTE)(val >> 8); L = (BYTE)(val & 0xFF); }

    // --- 100% ИСПРАВЛЕННАЯ СИНХРОННАЯ ФУНКЦИЯ ЧТЕНИЯ ИЗ ПОРТОВ ---
    BYTE PortIn(BYTE port) {
        // Оставляем сдвиг регистра (port >> 3) для полной стабильности вашего ядра Z80
        BYTE reg = (port >> 3) & 3;

        // 1. Дешифрация КР580ВВ55А
        if (!(port & 0x20)) {
            switch (reg) {
            case 0: return ppi_pa;
            case 1: {
                // Синхронизируем базовые сигналы телефонной линии (трубки)
                UpdateHardwareState();

                // AON.select = (PA&7)+1; опрос клавиатуры — БЕЗ сдвига фазы,
                // напрямую по номеру текущего выбранного разряда (см. keyboard_poll_row()).
                int sel = (ppi_pa & 0x07) + 1;

                // keys_pressed[12] индексируется как "123456789*0#"
                // {индекс клавиши для PB7 (старший бит фазы), индекс клавиши для PB6}
                static const int row_key_idx[6][2] = {
                    {6, 0},  // sel=1: '7' -> PB7, '1' -> PB6
                    {7, 1},  // sel=2: '8' -> PB7, '2' -> PB6
                    {8, 2},  // sel=3: '9' -> PB7, '3' -> PB6
                    {9, 3},  // sel=4: '*' -> PB7, '4' -> PB6
                    {10,4},  // sel=5: '0' -> PB7, '5' -> PB6
                    {11,5}   // sel=6: '#' -> PB7, '6' -> PB6
                };

                BYTE row = 0;
                if (sel >= 1 && sel <= 6) {
                    if (keys_pressed[row_key_idx[sel - 1][0]]) row |= 0x80;
                    if (keys_pressed[row_key_idx[sel - 1][1]]) row |= 0x40;
                }

                BYTE final_upper_bits = (row ^ 0xF0) & 0xF0;
                return (ppi_pb & 0x0F) | final_upper_bits;
            }
            case 2: return ppi_pc;
            case 3: return ppi_ctrl;
            }
        }

        // 2. Дешифрация КР580ВИ53
        if (!(port & 0x40)) {
            if (reg != 0x03) {
                return pit.ReadData(reg);
            }
        }

        return 0xFF; // Подтяжка шины к логической 1
    }

    // --- СКОРРЕКТИРОВАННАЯ ФУНКЦИЯ ЗАПИСИ В ПОРТЫ (PortOut) ---
    void PortOut(BYTE port, BYTE value) {
        // Вычисляем индекс регистра со смещением шины адреса
        BYTE reg = (port >> 3) & 3;

        // 1. Перехват порта вывода сегментов индикатора (строго по рабочему коду: if(!(adr&0x80)))
        if (!(port & 0x80)) {
            ir23_display_segments = value; // Маска палочек индикатора
            UpdateHardwareState();
            return;
        }

        // 2. Дешифрация портов КР580ВВ55А
        if (!(port & 0x20)) {
            switch (reg) {
            case 0: // Порт А: Изменение позиции индикатора
                ppi_pa = value;
                UpdateHardwareState();
                break;

            case 1: // Порт B
                ppi_pb = value;
                break;

            case 2: // Порт C: Звук и сканирование строк кнопок
                ppi_pc = value;
                // Синхронизация системного звука АОН
                rk_port_c_speaker_state = (value & 0x02) ? true : false;
                UpdateHardwareState();
                break;

            case 3: // Регистр Управляющего Слова (РУС)
                ppi_ctrl = value;
                UpdateHardwareState();
                break;
            }
            return;
        }

        // 3. Дешифрация КР580ВИ53
        if (!(port & 0x40)) {
            if (reg == 0x03) {
                pit.WriteControl(value);
            }
            else {
                pit.WriteData(reg, value);
            }
            return;
        }
    }

    int Step() {
        // Epilog helper: runs on function exit to handle EI delayed enable
        if (ei_delay_counter > 0) {
            ei_delay_counter--;
            if (ei_delay_counter == 0) {
                IFF1 = true; // enable interrupts after one instruction following EI
                IFF2 = true;
            }
        }
        
        // 1. Проверка маскируемого прерывания (50 Гц)
        if (int_pending && IFF1) {
            int_pending = false;
            halted = false;
            // Save previous IFF1 in IFF2 so RETN/RETI can restore it, then disable maskable interrupts
            IFF2 = IFF1;
            IFF1 = false;

            // Сохраняем текущий адрес возврата в стек
            WriteMem(--SP, PC >> 8);
            WriteMem(--SP, PC & 0xFF);

            // Обработка согласно режимам прерываний Z80
            if (IM == 1) {
                PC = 0x0038; // Стандартная обработка ZX Spectrum 48K
                return 13; // IM1: 13 T-states
            }
            else if (IM == 0) {
                // IM0: if an external device supplied a vector byte on the data bus,
                // the CPU will execute that opcode directly. We emulate a pragmatic
                // behavior: if interrupt_vector_bus_byte is non-zero and corresponds
                // to a RST n (0xC7/0xCF/.../0xFF) we jump to its vector; otherwise
                // fallback to IM1 (0x0038).
                if (interrupt_vector_bus_byte >= 0xC7 && interrupt_vector_bus_byte <= 0xFF) {
                    // RST n - compute vector
                    BYTE rst_index = (interrupt_vector_bus_byte - 0xC7) / 8;
                    WORD target = (WORD)(rst_index * 8);
                    PC = target;
                    return 13; // treat like RST timing
                }
                OutputDebugStringA("IM0: no vector byte or unsupported; treating as IM1 (0x0038)\n");
                PC = 0x0038;
                return 13;
            }
            else if (IM == 2) {
                WORD vector_addr = (I << 8) | 0xFF;
                WORD target_pc = ReadMem(vector_addr) | (ReadMem((WORD)(vector_addr + 1)) << 8);
                PC = target_pc;
                return 19; // IM2: 19 T-states (vector fetch + indirect)
            }
        }

        if (halted) return 4;

        WORD current_pc = PC;
        BYTE op = ReadMem(PC++);

        R = (R & 0x80) | ((R + 1) & 0x7F);

        const BYTE F_C = 0x01; const BYTE F_N = 0x02; const BYTE F_V = 0x04;
        const BYTE F_H = 0x10; const BYTE F_Z = 0x40; const BYTE F_S = 0x80;

        static const BYTE parity_table[256] = {
            4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
            0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
            0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
            4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
            0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
            4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
            4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
            0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4
        };

        // --- ИСПРАВЛЕННЫЕ МАКРОСЫ АЛУ Z80 ---
#define ALU_ADD(val) { \
    BYTE operand = val; \
    DWORD res = (DWORD)A + operand; \
    F = 0; \
    if ((res & 0xFF) == 0) F |= F_Z; \
    if (res & 0x80) F |= F_S; \
    if (res & 0x100) F |= F_C; \
    if ((A ^ operand ^ res) & 0x10) F |= F_H; \
    if ((A ^ res) & (operand ^ res) & 0x80) F |= F_V; \
    A = (BYTE)res; \
}

#define ALU_ADC(val) { \
    BYTE operand = val; \
    BYTE carry = (F & F_C) ? 1 : 0; \
    DWORD res = (DWORD)A + operand + carry; \
    F = 0; \
    if ((res & 0xFF) == 0) F |= F_Z; \
    if (res & 0x80) F |= F_S; \
    if (res & 0x100) F |= F_C; \
    if ((A ^ operand ^ res) & 0x10) F |= F_H; \
    if ((A ^ res) & (operand ^ res) & 0x80) F |= F_V; \
    A = (BYTE)res; \
}

#define ALU_SUB(val) { \
    BYTE operand = val; \
    BYTE old_a = A; \
    BYTE res = A - operand; \
    F = F_N; \
    if (res == 0) F |= F_Z; \
    if (res & 0x80) F |= F_S; \
    if (old_a < operand) F |= F_C; \
    if (((old_a & 0x0F) - (operand & 0x0F)) & 0x10) F |= F_H; \
    if (((old_a ^ operand) & (old_a ^ res)) & 0x80) F |= F_V; \
    A = res; \
}

#define ALU_CP(val) { \
    BYTE operand = val; \
    BYTE old_a = A; \
    BYTE res = A - operand; \
    F = F_N; \
    if (res == 0) F |= F_Z; \
    if (res & 0x80) F |= F_S; \
    if (old_a < operand) F |= F_C; \
    if (((old_a & 0x0F) - (operand & 0x0F)) & 0x10) F |= F_H; \
    if (((old_a ^ operand) & (old_a ^ res)) & 0x80) F |= F_V; \
}

#define ALU_SBC(val) { \
    BYTE operand = val; \
    BYTE old_a = A; \
    BYTE carry = (F & F_C) ? 1 : 0; \
    int t_res = (int)A - (int)operand - (int)carry; \
    BYTE res = (BYTE)t_res; \
    F = F_N; \
    if (res == 0) F |= F_Z; \
    if (res & 0x80) F |= F_S; \
    if (t_res < 0) F |= F_C; \
    if (((old_a & 0x0F) - (operand & 0x0F) - carry) & 0x10) F |= F_H; \
    if (((old_a ^ operand) & (old_a ^ res)) & 0x80) F |= F_V; \
    A = res; \
}

#define ALU_AND(val) { \
    A &= val; \
    F = F_H | (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0) | (parity_table[A & 0xFF] ? F_V : 0); \
}

#define ALU_XOR(val) { \
    A ^= val; \
    F = (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0) | (parity_table[A & 0xFF] ? F_V : 0); \
}

#define ALU_OR(val) { \
    A |= val; \
    F = (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0) | (parity_table[A & 0xFF] ? F_V : 0); \
}

        switch (op) {
        case 0x00: return 4; // NOP
        case 0x06: { B = ReadMem(PC++); return 7; } // LD B, n
        case 0x0E: { C = ReadMem(PC++); return 7; } // LD C, n
        case 0x16: { D = ReadMem(PC++); return 7; } // LD D, n
        case 0x1E: { E = ReadMem(PC++); return 7; } // LD E, n
        case 0x26: { H = ReadMem(PC++); return 7; } // LD H, n
        case 0x2E: { L = ReadMem(PC++); return 7; } // LD L, n
        case 0x36: { BYTE val = ReadMem(PC++); WriteMem(GetHL(), val); return 10; } // LD (HL), n
        case 0x3E: { A = ReadMem(PC++); return 7; } // LD A, n

        case 0x27: { // --- ПОЛНАЯ АППАРАТНАЯ СИНХРОНИЗАЦИЯ DAA Z80 ---
            BYTE correction = 0; bool set_carry = false;
            if ((F & F_H) || ((A & 0x0F) > 9)) { correction |= 0x06; }
            if ((F & F_C) || (A > 0x99)) { correction |= 0x60; set_carry = true; }
            BYTE old_a = A;
            if (F & F_N) { A -= correction; }
            else { A += correction; }
            F &= ~(F_H | F_Z | F_S | F_V);
            if (F & F_N) { if ((old_a & 0x0F) < (correction & 0x0F)) F |= F_H; }
            else { if ((old_a & 0x0F) + (correction & 0x0F) > 0x0F) F |= F_H; }
            if (A == 0) F |= F_Z; if (A & 0x80) F |= F_S;
            if (parity_table[A]) F |= F_V; if (set_carry) F |= F_C;
            return 4;
        }

        case 0x01: { C = ReadMem(PC++); B = ReadMem(PC++); return 10; } // LD BC, nn
        case 0x11: { E = ReadMem(PC++); D = ReadMem(PC++); return 10; } // LD DE, nn
        case 0x21: { L = ReadMem(PC++); H = ReadMem(PC++); return 10; } // LD HL, nn
        case 0x31: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); SP = (h << 8) | l; return 10; } // LD SP, nn

        case 0x0A: { A = ReadMem(GetBC()); return 7; } // LD A, (BC)
        case 0x1A: { A = ReadMem(GetDE()); return 7; } // LD A, (DE)
        case 0x02: { WriteMem(GetBC(), A); return 7; } // LD (BC), A
        case 0x12: { WriteMem(GetDE(), A); return 7; } // LD (DE), A
        case 0x22: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); WORD adr = (h << 8) | l; WriteMem(adr, L); WriteMem((WORD)(adr + 1), H); return 16; } // LD (nn), HL
        case 0x2A: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); WORD adr = (h << 8) | l; L = ReadMem(adr); H = ReadMem((WORD)(adr + 1)); return 16; } // LD HL, (nn)
        case 0x32: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); WriteMem((h << 8) | l, A); return 13; } // LD (nn), A
        case 0x3A: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); A = ReadMem((h << 8) | l); return 13; } // LD A, (nn)

        case 0x09: { DWORD res = GetHL() + GetBC(); F = (F & (F_Z | F_S | F_V)) | (((GetHL() & 0xFFF) + (GetBC() & 0xFFF) & 0x1000) ? F_H : 0) | ((res & 0x10000) ? F_C : 0); SetHL((WORD)res); return 11; }
        case 0x19: { DWORD res = GetHL() + GetDE(); F = (F & (F_Z | F_S | F_V)) | (((GetHL() & 0xFFF) + (GetDE() & 0xFFF) & 0x1000) ? F_H : 0) | ((res & 0x10000) ? F_C : 0); SetHL((WORD)res); return 11; }
        case 0x29: { DWORD res = GetHL() + GetHL(); F = (F & (F_Z | F_S | F_V)) | (((GetHL() & 0xFFF) + (GetHL() & 0xFFF) & 0x1000) ? F_H : 0) | ((res & 0x10000) ? F_C : 0); SetHL((WORD)res); return 11; }
        case 0x39: { DWORD res = GetHL() + SP; F = (F & (F_Z | F_S | F_V)) | (((GetHL() & 0xFFF) + (SP & 0xFFF) & 0x1000) ? F_H : 0) | ((res & 0x10000) ? F_C : 0); SetHL((WORD)res); return 11; }

        case 0x03: { WORD bc = GetBC(); bc++; SetBC(bc); return 6; } // INC BC
        case 0x0B: { WORD bc = GetBC(); bc--; SetBC(bc); return 6; } // DEC BC
        case 0x13: { WORD de = GetDE(); de++; SetDE(de); return 6; } // INC DE
        case 0x1B: { WORD de = GetDE(); de--; SetDE(de); return 6; } // DEC DE
        case 0x23: { WORD hl = GetHL(); hl++; SetHL(hl); return 6; } // INC HL
        case 0x2B: { WORD hl = ((WORD)H << 8) | L; hl--; H = (BYTE)(hl >> 8); L = (BYTE)(hl & 0xFF); return 6; } // DEC HL
        case 0x33: { SP++; return 6; } // INC SP
        case 0x3B: { SP--; return 6; } // DEC SP

        case 0x07: { BYTE c = (A & 0x80) ? 1 : 0; A = (A << 1) | c; F = (F & (F_Z | F_S | F_V)) | (c ? F_C : 0); return 4; } // RLCA
        case 0x17: { BYTE c = (A & 0x80) ? 1 : 0; BYTE old_c = F & F_C; A = (A << 1) | old_c; F = (F & (F_Z | F_S | F_V)) | (c ? F_C : 0); return 4; } // RLA
        case 0x0F: { BYTE c = A & 1; A = (A >> 1) | (c << 7); F = (F & (F_Z | F_S | F_V)) | (c ? F_C : 0); return 4; } // RRCA
        case 0x1F: { BYTE c = A & 1; BYTE old_c = F & F_C; A = (A >> 1) | (old_c << 7); F = (F & (F_Z | F_S | F_V)) | (c ? F_C : 0); return 4; } // RRA

        case 0x10: { signed char offset = (signed char)ReadMem(PC++); BYTE current_b = B; current_b--; B = current_b; if (B != 0) { PC = (WORD)((int)PC + offset); return 13; } return 8; } // DJNZ
        case 0x18: { signed char offset = (signed char)ReadMem(PC++); PC = (WORD)((int)PC + offset); return 12; } // JR e
        case 0x20: { signed char offset = (signed char)ReadMem(PC++); if (!(F & F_Z)) { PC = (WORD)((int)PC + offset); return 12; } return 7; } // JR NZ, e
        case 0x28: { signed char offset = (signed char)ReadMem(PC++); if (F & F_Z) { PC = (WORD)((int)PC + offset); return 12; } return 7; } // JR Z, e
        case 0x30: { signed char offset = (signed char)ReadMem(PC++); if (!(F & F_C)) { PC = (WORD)((int)PC + offset); return 12; } return 7; } // JR NC, e
        case 0x38: { signed char offset = (signed char)ReadMem(PC++); if (F & F_C) { PC = (WORD)((int)PC + offset); return 12; } return 7; } // JR C, e

        case 0xD3: { BYTE p = ReadMem(PC++); PortOut((WORD)(((WORD)A << 8) | p), A); return 11; } // OUT (n), A
        case 0xDB: { BYTE p = ReadMem(PC++); A = PortIn((WORD)(((WORD)A << 8) | p)); F &= ~(F_N | F_H); if (A == 0) F |= F_Z; if (A & 0x80) F |= F_S; return 11; } // IN A, (n)

        case 0x76: { halted = true; return 4; } // HALT
        case 0xFB: { /* EI - enable interrupts after one instruction */ ei_delay_counter = 2; return 4; } // EI (delayed)
        case 0xF3: { IFF1 = false; IFF2 = false; ei_delay_counter = 0; return 4; } // DI

        case 0x08: { BYTE t = A; A = A_alt; A_alt = t; t = F; F = F_alt; F_alt = t; return 4; } // EX AF, AF'
        case 0xD9: { BYTE t; t = B; B = B_alt; B_alt = t; t = C; C = C_alt; C_alt = t; t = D; D = D_alt; D_alt = t; t = E; E = E_alt; E_alt = t; t = H; H = H_alt; H_alt = t; t = L; L = L_alt; L_alt = t; return 4; } // EXX
        case 0xEB: { BYTE t = D; D = H; H = t; t = E; E = L; L = t; return 4; } // EX DE, HL
        case 0xE3: { BYTE low_stack = ReadMem(SP); BYTE high_stack = ReadMem((WORD)(SP + 1)); WriteMem(SP, L); WriteMem((WORD)(SP + 1), H); L = low_stack; H = high_stack; return 19; } // EX (SP), HL
        case 0x2F: { A = ~A; F |= F_N | F_H; return 4; } // CPL
        case 0x37: { F = (F & (F_Z | F_S | F_V)) | F_C; return 4; } // SCF
        case 0x3F: { F = (F & (F_Z | F_S | F_V)) | ((F & F_C) ? F_H : F_C); return 4; } // CCF

#define INC_REG(reg) { \
    BYTE old = reg; reg++; \
    F = (F & F_C) | (reg == 0 ? F_Z : 0) | (reg & 0x80 ? F_S : 0) | ((((old & 0x0F) + 1) & 0x10) ? F_H : 0) | (old == 0x7F ? F_V : 0); \
    return 4; \
}

#define DEC_REG(reg) { \
    BYTE old = reg; reg--; \
    F = (F & F_C) | F_N | (reg == 0 ? F_Z : 0) | (reg & 0x80 ? F_S : 0) | (((old & 0x0F) < (reg & 0x0F)) ? F_H : 0) | (old == 0x80 ? F_V : 0); \
    return 4; \
}

        case 0x04: { INC_REG(B); break; }
        case 0x0C: { INC_REG(C); break; }
        case 0x14: { INC_REG(D); break; }
        case 0x1C: { INC_REG(E); break; }
        case 0x24: { INC_REG(H); break; }
        case 0x2C: { INC_REG(L); break; }
        case 0x3C: { INC_REG(A); break; }

        case 0x05: { DEC_REG(B); break; }
        case 0x0D: { DEC_REG(C); break; }
        case 0x15: { DEC_REG(D); break; }
        case 0x1D: { DEC_REG(E); break; }
        case 0x25: { DEC_REG(H); break; }
        case 0x2D: { DEC_REG(L); break; }
        case 0x3D: { DEC_REG(A); break; }

        case 0x34: { BYTE v = ReadMem(GetHL()); BYTE old = v; v++; WriteMem(GetHL(), v); F = (F & F_C) | (v == 0 ? F_Z : 0) | (v & 0x80 ? F_S : 0) | ((old & 0x0F) == 0x0F ? F_H : 0) | (old == 0x7F ? F_V : 0); return 11; }
        case 0x35: { BYTE v = ReadMem(GetHL()); BYTE old = v; v--; WriteMem(GetHL(), v); F = (F & F_C) | F_N | (v == 0 ? F_Z : 0) | (v & 0x80 ? F_S : 0) | ((old & 0x0F) == 0x00 ? F_H : 0) | (old == 0x80 ? F_V : 0); return 11; }

        case 0x80: ALU_ADD(B); return 4; case 0x81: ALU_ADD(C); return 4; case 0x82: ALU_ADD(D); return 4; case 0x83: ALU_ADD(E); return 4;
        case 0x84: ALU_ADD(H); return 4; case 0x85: ALU_ADD(L); return 4; case 0x86: ALU_ADD(ReadMem(GetHL())); return 7; case 0x87: ALU_ADD(A); return 4;
        case 0x88: ALU_ADC(B); return 4; case 0x89: ALU_ADC(C); return 4; case 0x8A: ALU_ADC(D); return 4; case 0x8B: ALU_ADC(E); return 4;
        case 0x8C: ALU_ADC(H); return 4; case 0x8D: ALU_ADC(L); return 4; case 0x8E: ALU_ADC(ReadMem(GetHL())); return 7; case 0x8F: ALU_ADC(A); return 4;
        case 0x90: ALU_SUB(B); return 4; case 0x91: ALU_SUB(C); return 4; case 0x92: ALU_SUB(D); return 4; case 0x93: ALU_SUB(E); return 4;
        case 0x94: ALU_SUB(H); return 4; case 0x95: ALU_SUB(L); return 4; case 0x96: ALU_SUB(ReadMem(GetHL())); return 7; case 0x97: ALU_SUB(A); return 4;
        case 0x98: ALU_SBC(B); return 4; case 0x99: ALU_SBC(C); return 4; case 0x9A: ALU_SBC(D); return 4; case 0x9B: ALU_SBC(E); return 4;
        case 0x9C: ALU_SBC(H); return 4; case 0x9D: ALU_SBC(L); return 4; case 0x9E: ALU_SBC(ReadMem(GetHL())); return 7; case 0x9F: ALU_SBC(A); return 4;
        case 0xA0: ALU_AND(B); return 4; case 0xA1: ALU_AND(C); return 4; case 0xA2: ALU_AND(D); return 4; case 0xA3: ALU_AND(E); return 4;
        case 0xA4: ALU_AND(H); return 4; case 0xA5: ALU_AND(L); return 4; case 0xA6: ALU_AND(ReadMem(GetHL())); return 7; case 0xA7: ALU_AND(A); return 4;
        case 0xAF: ALU_XOR(A); return 4; case 0xA8: ALU_XOR(B); return 4; case 0xA9: ALU_XOR(C); return 4; case 0xAA: ALU_XOR(D); return 4;
        case 0xAB: ALU_XOR(E); return 4; case 0xAC: ALU_XOR(H); return 4; case 0xAD: ALU_XOR(L); return 4; case 0xAE: ALU_XOR(ReadMem(GetHL())); return 7;
        case 0xB0: ALU_OR(B); return 4; case 0xB1: ALU_OR(C); return 4; case 0xB2: ALU_OR(D); return 4; case 0xB3: ALU_OR(E); return 4;
        case 0xB4: ALU_OR(H); return 4; case 0xB5: ALU_OR(L); return 4; case 0xB6: ALU_OR(ReadMem(GetHL())); return 7; case 0xB7: ALU_OR(A); return 4;
        case 0xB8: ALU_CP(B); return 4; case 0xB9: ALU_CP(C); return 4; case 0xBA: ALU_CP(D); return 4; case 0xBB: ALU_CP(E); return 4;
        case 0xBC: ALU_CP(H); return 4; case 0xBD: ALU_CP(L); return 4; case 0xBE: ALU_CP(ReadMem(GetHL())); return 7; case 0xBF: ALU_CP(A); return 4;

        case 0xC6: { ALU_ADD(ReadMem(PC++)); return 7; }
        case 0xCE: { ALU_ADC(ReadMem(PC++)); return 7; }
        case 0xD6: { ALU_SUB(ReadMem(PC++)); return 7; }
        case 0xDE: { ALU_SBC(ReadMem(PC++)); return 7; }
        case 0xE6: { ALU_AND(ReadMem(PC++)); return 7; }
        case 0xEE: { ALU_XOR(ReadMem(PC++)); return 7; }
        case 0xF6: { ALU_OR(ReadMem(PC++)); return 7; }
        case 0xFE: { ALU_CP(ReadMem(PC++)); return 7; }

        case 0xC3: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); PC = (h << 8) | l; return 10; }
        case 0xE9: { PC = GetHL(); return 4; }
        case 0xF9: { SP = GetHL(); return 6; }
        case 0xCD: { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); WriteMem(--SP, (BYTE)(PC >> 8)); WriteMem(--SP, (BYTE)(PC & 0xFF)); PC = (h << 8) | l; return 17; }
        case 0xC9: { BYTE l = ReadMem(SP++); BYTE h = ReadMem(SP++); PC = (h << 8) | l; return 10; }

#define JP_COND(cond) { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); if (cond) PC = (h << 8) | l; return 10; }
#define CALL_COND(cond) { WORD l = ReadMem(PC++); WORD h = ReadMem(PC++); if (cond) { WriteMem(--SP, (BYTE)(PC >> 8)); WriteMem(--SP, (BYTE)(PC & 0xFF)); PC = (h << 8) | l; return 17; } return 10; }
#define RET_COND(cond) { if (cond) { BYTE l = ReadMem(SP++); BYTE h = ReadMem(SP++); PC = (h << 8) | l; return 11; } return 5; }

        case 0xC2: JP_COND(!(F & F_Z)); case 0xCA: JP_COND(F & F_Z); case 0xD2: JP_COND(!(F & F_C)); case 0xDA: JP_COND(F & F_C);
        case 0xE2: JP_COND(!(F & F_V)); case 0xEA: JP_COND(F & F_V); case 0xF2: JP_COND(!(F & F_S)); case 0xFA: JP_COND(F & F_S);
        case 0xC4: CALL_COND(!(F & F_Z)); case 0xCC: CALL_COND(F & F_Z); case 0xD4: CALL_COND(!(F & F_C)); case 0xDC: CALL_COND(F & F_C);
        case 0xE4: CALL_COND(!(F & F_V)); case 0xEC: CALL_COND(F & F_V); case 0xF4: CALL_COND(!(F & F_S)); case 0xFC: CALL_COND(F & F_S);
        case 0xC0: RET_COND(!(F & F_Z)); case 0xC8: RET_COND(F & F_Z); case 0xD0: RET_COND(!(F & F_C)); case 0xD8: RET_COND(F & F_C);
        case 0xE0: RET_COND(!(F & F_V)); case 0xE8: RET_COND(F & F_V); case 0xF0: RET_COND(!(F & F_S)); case 0xF8: RET_COND(F & F_S);

#define PUSH_REG(h, l) { WriteMem(--SP, h); WriteMem(--SP, l); return 11; }
#define POP_REG(h, l) { l = ReadMem(SP++); h = ReadMem(SP++); return 10; }
        case 0xC5: PUSH_REG(B, C); case 0xD5: PUSH_REG(D, E); case 0xE5: PUSH_REG(H, L); case 0xF5: PUSH_REG(A, F);
        case 0xC1: POP_REG(B, C); case 0xD1: POP_REG(D, E); case 0xE1: POP_REG(H, L); case 0xF1: POP_REG(A, F);

#define RST_VEC(adr) { WriteMem(--SP, (BYTE)(PC >> 8)); WriteMem(--SP, (BYTE)(PC & 0xFF)); PC = adr; return 11; }
        case 0xC7: RST_VEC(0x00); case 0xCF: RST_VEC(0x08); case 0xD7: RST_VEC(0x10); case 0xDF: RST_VEC(0x18);
        case 0xE7: RST_VEC(0x20); case 0xEF: RST_VEC(0x28); case 0xF7: RST_VEC(0x30); case 0xFF: RST_VEC(0x38);

        case 0xCB: {
            BYTE subOp = ReadMem(PC++);
            BYTE type = subOp >> 6;
            BYTE bit = (subOp >> 3) & 7;
            BYTE regIdx = subOp & 7;
            BYTE val = 0;
            if (regIdx == 0) val = B; else if (regIdx == 1) val = C;
            else if (regIdx == 2) val = D; else if (regIdx == 3) val = E;
            else if (regIdx == 4) val = H; else if (regIdx == 5) val = L;
            else if (regIdx == 6) val = ReadMem(GetHL()); else if (regIdx == 7) val = A;

            if (type == 0) {
                BYTE c = 0;
                switch (bit) {
                case 0: c = val >> 7; val = (val << 1) | c; break; // RLC
                case 1: c = val & 1; val = (val >> 1) | (c << 7); break; // RRC
                case 2: c = val >> 7; val = (val << 1) | ((F & F_C) ? 1 : 0); break; // RL
                case 3: c = val & 1; val = (val >> 1) | (((F & F_C) ? 1 : 0) << 7); break; // RR
                case 4: c = val >> 7; val <<= 1; break; // SLA
                case 5: c = val & 1; val = (signed char)val >> 1; break; // SRA
                case 6: c = val >> 7; val = (val << 1) | 1; break; // SLL
                case 7: c = val & 1; val >>= 1; break; // SRL
                }
                F = (val == 0 ? F_Z : 0) | (val & 0x80 ? F_S : 0) | (c ? F_C : 0) | (parity_table[val & 0xFF] ? F_V : 0);
            }
            else if (type == 1) {
                F = (F & F_C) | F_H | (!(val & (1 << bit)) ? F_Z : 0) | (bit == 7 && (val & 0x80) ? F_S : 0);
            }
            else if (type == 2) { val &= ~(1 << bit); }
            else if (type == 3) { val |= (1 << bit); }

            if (regIdx == 0) B = val; else if (regIdx == 1) C = val;
            else if (regIdx == 2) D = val; else if (regIdx == 3) E = val;
            else if (regIdx == 4) H = val; else if (regIdx == 5) L = val;
            else if (regIdx == 6) WriteMem(GetHL(), val); else if (regIdx == 7) A = val;
            return (regIdx == 6) ? 15 : 8;
        }

        case 0xED: {
            BYTE subOp = ReadMem(PC++);

            // 1. Команды сложения и вычитания 16-бит с переносом: ADC HL, ss / SBC HL, ss
            if ((subOp & 0xCF) == 0x42 || (subOp & 0xCF) == 0x4A) {
                DWORD hl_val = GetHL(); DWORD ss_val = 0;
                BYTE ss = (subOp >> 4) & 3;
                if (ss == 0) ss_val = GetBC(); else if (ss == 1) ss_val = GetDE();
                else if (ss == 2) ss_val = GetHL(); else if (ss == 3) ss_val = SP;
                DWORD carry = (F & F_C) ? 1 : 0;
                bool is_sbc = ((subOp & 0x0F) == 0x02);
                if (is_sbc) {
                    DWORD res = (hl_val - ss_val - carry) & 0xFFFFFFFF; F = F_N;
                    if ((res & 0xFFFF) == 0) F |= F_Z; if (res & 0x8000) F |= F_S;
                    if (hl_val < (ss_val + carry)) F |= F_C;
                    if (((hl_val & 0x0FFF) - (ss_val & 0x0FFF) - carry) & 0x1000) F |= F_H;
                    if (((hl_val ^ ss_val) & (hl_val ^ res)) & 0x8000) F |= F_V;
                    SetHL((WORD)res);
                }
                else {
                    DWORD res = hl_val + ss_val + carry; F = 0;
                    if ((res & 0xFFFF) == 0) F |= F_Z; if (res & 0x8000) F |= F_S;
                    if (res & 0x10000) F |= F_C;
                    if (((hl_val & 0x0FFF) + (ss_val & 0x0FFF) + carry) & 0x1000) F |= F_H;
                    if (((hl_val ^ ss_val ^ 0x8000) & (ss_val ^ res)) & 0x8000) F |= F_V;
                    SetHL((WORD)res);
                }
                return 15;
            }

            // 2. Команды загрузки 16-битных регистров из памяти: LD (nn), dd / LD dd, (nn)
            if ((subOp & 0xCF) == 0x43 || (subOp & 0xCF) == 0x4B) {
                WORD l = ReadMem(PC++); WORD h = ReadMem(PC++);
                WORD adr = (h << 8) | l; BYTE dd = (subOp >> 4) & 3;
                bool is_store = ((subOp & 0x0F) == 0x03);
                if (is_store) {
                    WORD val = (dd == 0) ? GetBC() : (dd == 1 ? GetDE() : (dd == 2 ? GetHL() : SP));
                    WriteMem(adr, val & 0xFF); WriteMem((WORD)(adr + 1), val >> 8);
                }
                else {
                    WORD val = ReadMem(adr) | (ReadMem((WORD)(adr + 1)) << 8);
                    if (dd == 0) SetBC(val); else if (dd == 1) SetDE(val);
                    else if (dd == 2) SetHL(val); else if (dd == 3) SP = val;
                }
                return 20;
            }

            // 3. Аппаратно точная дешифрация команд ввода-вывода: IN r, (C) / OUT (C), r
            if ((subOp & 0xC0) == 0x40 && (subOp & 0x07) <= 1) {
                BYTE regIdx = ((subOp & 0xFF) >> 3) & 7;
                bool is_out = ((subOp & 1) != 0);
                if (is_out) {
                    BYTE srcVal = 0;
                    if (regIdx == 0) srcVal = B; else if (regIdx == 1) srcVal = C;
                    else if (regIdx == 2) srcVal = D; else if (regIdx == 3) srcVal = E;
                    else if (regIdx == 4) srcVal = H; else if (regIdx == 5) srcVal = L;
                    else if (regIdx == 6) srcVal = 0; else if (regIdx == 7) srcVal = A;
                    PortOut(GetBC(), srcVal); return 12;
                }
                else {
                    BYTE inputVal = PortIn(GetBC());
                    if (regIdx == 0) B = inputVal; else if (regIdx == 1) C = inputVal;
                    else if (regIdx == 2) D = inputVal; else if (regIdx == 3) E = inputVal;
                    else if (regIdx == 4) H = inputVal; else if (regIdx == 5) L = inputVal;
                    else if (regIdx == 7) A = inputVal;
                    F = (F & F_C) | (inputVal == 0 ? F_Z : 0) | (inputVal & 0x80 ? F_S : 0) | (parity_table[inputVal & 0xFF] ? F_V : 0);
                    return 12;
                }
            }

            // 4. Одиночные подкоманды префикса 0xED
            switch (subOp) {
            case 0xB0: { // LDIR
                WORD src = GetHL(); WORD dest = GetDE(); WORD len = GetBC();
                while (len > 0) { WriteMem(dest, ReadMem(src)); src++; dest++; len--; }
                SetHL(src); SetDE(dest); SetBC(0); F &= ~(F_N | F_H | F_V); return 21;
            }
            case 0xB8: { // LDDR
                WORD src = GetHL(); WORD dest = GetDE(); WORD len = GetBC();
                while (len > 0) { WriteMem(dest, ReadMem(src)); src--; dest--; len--; }
                SetHL(src); SetDE(dest); SetBC(0); F &= ~(F_N | F_H | F_V); return 21;
            }
            case 0xA0: { // LDI
                WriteMem(GetDE(), ReadMem(GetHL())); SetHL(GetHL() + 1); SetDE(GetDE() + 1); SetBC(GetBC() - 1);
                F &= ~(F_N | F_H | F_V); if (GetBC() != 0) F |= F_V; return 14;
            }
            case 0xA8: { // LDD
                WriteMem(GetDE(), ReadMem(GetHL())); SetHL(GetHL() - 1); SetDE(GetDE() - 1); SetBC(GetBC() - 1);
                F &= ~(F_N | F_H | F_V); if (GetBC() != 0) F |= F_V; return 14;
            }
            case 0xA1: case 0xB1: { // CPI / CPIR
                BYTE val = ReadMem(GetHL()); int res = A - val; SetHL(GetHL() + 1); SetBC(GetBC() - 1);
                F = (F & F_C) | F_N | (res == 0 ? F_Z : 0) | (res & 0x80 ? F_S : 0) | (((A & 0x0F) - (val & 0x0F)) & 0x10 ? F_H : 0) | (GetBC() != 0 ? F_V : 0);
                if (subOp == 0xB1 && GetBC() != 0 && res != 0) { PC -= 2; return 21; } return 16;
            }
            case 0xA2: case 0xB2: { // INI / INIR
                BYTE val = PortIn(GetBC()); WriteMem(GetHL(), val); B--; SetHL(GetHL() + 1);
                F = F_N | (B == 0 ? F_Z : 0) | (B & 0x80 ? F_S : 0); if (subOp == 0xB2 && B != 0) { PC -= 2; return 21; } return 16;
            }
            case 0xA3: case 0xB3: { // OUTI / OTIR
                BYTE val = ReadMem(GetHL()); PortOut(GetBC(), val); B--; SetHL(GetHL() + 1);
                F = F_N | (B == 0 ? F_Z : 0) | (B & 0x80 ? F_S : 0); if (subOp == 0xB3 && B != 0) { PC -= 2; return 21; } return 16;
            }
            case 0x44: { A = 0 - A; F = F_N | (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0); return 8; } // NEG
            case 0x45: { IFF1 = IFF2; BYTE l = ReadMem(SP++); BYTE h = ReadMem(SP++); PC = (h << 8) | l; return 14; } // RETN
            case 0x4D: { IFF1 = IFF2; BYTE l = ReadMem(SP++); BYTE h = ReadMem(SP++); PC = (h << 8) | l; return 14; } // RETI
            case 0x46: { IM = 0; return 8; } case 0x56: { IM = 1; return 8; } case 0x5E: { IM = 2; return 8; }
            case 0x47: { I = A; return 9; } case 0x57: { A = I; F = (F & F_C) | (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0) | (IFF2 ? F_V : 0); return 9; }
            case 0x4F: { R = A; return 9; } case 0x5F: { A = R; F = (F & F_C) | (A == 0 ? F_Z : 0) | (A & 0x80 ? F_S : 0) | (IFF2 ? F_V : 0); return 9; }
            }
            return 8;
        }

        case 0xDD:
        case 0xFD: {
            const bool use_iy = (op == 0xFD);
            BYTE subOp = ReadMem(PC++);

            // DD CB d op / FD CB d op
            if (subOp == 0xCB) {
                signed char disp = (signed char)ReadMem(PC++);
                BYTE cbOp = ReadMem(PC++);
                WORD index = use_iy ? IY : IX;
                WORD addr = (WORD)(index + disp);
                BYTE val = ReadMem(addr);
                BYTE type = cbOp >> 6;
                BYTE bit = (cbOp >> 3) & 7;
                BYTE regIdx = cbOp & 7;

                if (type == 1) {
                    BYTE mask = (BYTE)(1u << bit);
                    // BIT b,(IX/IY+d): Z is set when the tested bit is zero,
                    // S is meaningful only for bit 7, H=1, N=0, C preserved.
                    F = (F & F_C) | F_H |
                        ((val & mask) ? 0 : F_Z) |
                        ((bit == 7 && (val & 0x80)) ? F_S : 0) |
                        (parity_table[val] && bit != 7 ? F_V : 0);
                    return 20;
                }

                BYTE result = val;
                BYTE carry_out = 0;
                switch (bit) {
                case 0: carry_out = (BYTE)(result >> 7); result = (BYTE)((result << 1) | carry_out); break; // RLC
                case 1: carry_out = (BYTE)(result & 1); result = (BYTE)((result >> 1) | (carry_out << 7)); break; // RRC
                case 2: carry_out = (BYTE)(result >> 7); result = (BYTE)((result << 1) | ((F & F_C) ? 1 : 0)); break; // RL
                case 3: carry_out = (BYTE)(result & 1); result = (BYTE)((result >> 1) | (((F & F_C) ? 1 : 0) << 7)); break; // RR
                case 4: carry_out = (BYTE)(result >> 7); result = (BYTE)(result << 1); break; // SLA
                case 5: carry_out = (BYTE)(result & 1); result = (BYTE)(((signed char)result) >> 1); break; // SRA
                case 6: carry_out = (BYTE)(result >> 7); result = (BYTE)((result << 1) | 1); break; // SLL
                case 7: carry_out = (BYTE)(result & 1); result = (BYTE)(result >> 1); break; // SRL
                }

                if (type == 0) {
                    F = (result == 0 ? F_Z : 0) |
                        (result & 0x80 ? F_S : 0) |
                        (parity_table[result] ? F_V : 0) |
                        (carry_out ? F_C : 0);
                }
                else if (type == 2) {
                    result = (BYTE)(val & ~(1u << bit));
                }
                else {
                    result = (BYTE)(val | (1u << bit));
                }

                WriteMem(addr, result);
                // DD/FD CB forms with r=6 are memory-only. Otherwise the
                // transformed byte is also copied into the selected register.
                if (regIdx == 0) B = result;
                else if (regIdx == 1) C = result;
                else if (regIdx == 2) D = result;
                else if (regIdx == 3) E = result;
                else if (regIdx == 4) H = result;
                else if (regIdx == 5) L = result;
                else if (regIdx == 7) A = result;
                return 23;
            }

             // --- ПОЛНАЯ ПОДДЕРЖКА ДОКУМЕНТИРОВАННЫХ И НЕДОКУМЕНТИРОВАННЫХ КОМАНД IX/IY ---
            WORD& index = use_iy ? IY : IX;

            // Выделяем ссылки на половинки регистров для недокументированных опкодов
            BYTE& index_h = use_iy ? *(BYTE*)((BYTE*)&IY + 1) : *(BYTE*)((BYTE*)&IX + 1);
            BYTE& index_l = use_iy ? *(BYTE*)((BYTE*)&IY) : *(BYTE*)((BYTE*)&IX);

            if (subOp == 0x21) {
                WORD l = ReadMem(PC++), h = ReadMem(PC++);
                index = (WORD)((h << 8) | l);
                return 14;
            }
            if (subOp == 0x22) {
                WORD l = ReadMem(PC++), h = ReadMem(PC++);
                WORD adr = (WORD)((h << 8) | l);
                WriteMem(adr, (BYTE)(index & 0xFF));
                WriteMem((WORD)(adr + 1), (BYTE)(index >> 8));
                return 20;
            }
            if (subOp == 0x2A) {
                WORD l = ReadMem(PC++), h = ReadMem(PC++);
                WORD adr = (WORD)((h << 8) | l);
                index = (WORD)(ReadMem(adr) | (ReadMem((WORD)(adr + 1)) << 8));
                return 20;
            }
            if (subOp == 0x23) { index = (WORD)(index + 1); return 10; }
            if (subOp == 0x2B) { index = (WORD)(index - 1); return 10; }

            if (subOp == 0xE5) {
                WriteMem(--SP, (BYTE)(index >> 8));
                WriteMem(--SP, (BYTE)index);
                return 15;
            }
            if (subOp == 0xE1) {
                BYTE l = ReadMem(SP); SP = (WORD)(SP + 1);
                BYTE h = ReadMem(SP); SP = (WORD)(SP + 1);
                index = (WORD)((h << 8) | l);
                return 14;
            }
            if (subOp == 0xE3) {
                BYTE low = ReadMem(SP);
                BYTE high = ReadMem((WORD)(SP + 1));
                WriteMem(SP, (BYTE)index);
                WriteMem((WORD)(SP + 1), (BYTE)(index >> 8));
                index = (WORD)(low | (high << 8));
                return 23;
            }
            if (subOp == 0xE9) { PC = index; return 8; }
            if (subOp == 0xF9) { SP = index; return 10; }

            // ADD IX/IY,rr
            if ((subOp & 0xCF) == 0x09) {
                WORD ss_val = 0;
                BYTE ss = (subOp >> 4) & 3;
                if (ss == 0) ss_val = GetBC();
                else if (ss == 1) ss_val = GetDE();
                else if (ss == 2) ss_val = index;
                else ss_val = SP;
                DWORD res = (DWORD)index + ss_val;
                F = (F & (F_Z | F_S | F_V)) |
                    ((((index & 0x0FFF) + (ss_val & 0x0FFF)) > 0x0FFF) ? F_H : 0) |
                    ((res & 0x10000) ? F_C : 0);
                index = (WORD)res;
                return 15;
            }

            // LD (IX+d),n / LD (IY+d),n -- this was the missing instruction
            // that desynchronised PC immediately after ROM 128E.
            if (subOp == 0x36) {
                signed char disp = (signed char)ReadMem(PC++);
                BYTE value = ReadMem(PC++);
                WriteMem((WORD)(index + disp), value);
                return 19;
            }

            // INC/DEC (IX+d) / (IY+d)
            if (subOp == 0x34 || subOp == 0x35) {
                signed char disp = (signed char)ReadMem(PC++);
                WORD addr = (WORD)(index + disp);
                BYTE old = ReadMem(addr);
                BYTE res = (subOp == 0x34) ? (BYTE)(old + 1) : (BYTE)(old - 1);

                BYTE old_c = F & F_C;
                if (subOp == 0x34) {
                    F = old_c |
                        (res == 0 ? F_Z : 0) |
                        (res & 0x80 ? F_S : 0) |
                        (((old & 0x0F) == 0x0F) ? F_H : 0) |
                        (old == 0x7F ? F_V : 0);
                }
                else {
                    F = old_c | F_N |
                        (res == 0 ? F_Z : 0) |
                        (res & 0x80 ? F_S : 0) |
                        (((old & 0x0F) == 0x00) ? F_H : 0) |
                        (old == 0x80 ? F_V : 0);
                }
                WriteMem(addr, res);
                return 23;
            }

            // LD r,(IX+d)/(IY+d), LD (IX+d),r
            if ((subOp & 0xC0) == 0x40 && subOp != 0x76) {
                BYTE destIdx = (subOp >> 3) & 7;
                BYTE srcIdx = subOp & 7;

                if (srcIdx == 6) {
                    signed char disp = (signed char)ReadMem(PC++);
                    BYTE memVal = ReadMem((WORD)(index + disp));
                    if (destIdx == 0) B = memVal;
                    else if (destIdx == 1) C = memVal;
                    else if (destIdx == 2) D = memVal;
                    else if (destIdx == 3) E = memVal;
                    else if (destIdx == 4) H = memVal;
                    else if (destIdx == 5) L = memVal;
                    else if (destIdx == 7) A = memVal;
                    return 19;
                }

                if (destIdx == 6) {
                    signed char disp = (signed char)ReadMem(PC++);
                    BYTE srcVal = 0;
                    if (srcIdx == 0) srcVal = B;
                    else if (srcIdx == 1) srcVal = C;
                    else if (srcIdx == 2) srcVal = D;
                    else if (srcIdx == 3) srcVal = E;
                    else if (srcIdx == 4) srcVal = H;
                    else if (srcIdx == 5) srcVal = L;
                    else if (srcIdx == 7) srcVal = A;
                    WriteMem((WORD)(index + disp), srcVal);
                    return 19;
                }
            }

            // ALU A,(IX+d)/(IY+d): 86/8E/96/9E/A6/AE/B6/BE
            if ((subOp & 0xC7) == 0x86) {
                signed char disp = (signed char)ReadMem(PC++);
                BYTE value = ReadMem((WORD)(index + disp));
                switch (subOp) {
                case 0x86: ALU_ADD(value); break;
                case 0x8E: ALU_ADC(value); break;
                case 0x96: ALU_SUB(value); break;
                case 0x9E: ALU_SBC(value); break;
                case 0xA6: ALU_AND(value); break;
                case 0xAE: ALU_XOR(value); break;
                case 0xB6: ALU_OR(value); break;
                case 0xBE: ALU_CP(value); break;
                }
                return 19;
            }

            // --- ИСПРАВЛЕНО: ДЕКОДИРОВАНИЕ НЕДОКУМЕНТИРОВАННЫХ ОПКОДОВ ДЛЯ ПОЛОВИНОК IX/IY ---
            // Сюда попадает ваш опкод 0x54 (LD D, IXH) и все операции ALU над половинками регистров
            {
                BYTE destReg = (subOp >> 3) & 7;
                BYTE srcReg = subOp & 7;

                // Перенаправление обращений к H/L на половинки IXH/IXL (или IYH/IYL)
                auto get_val = [&](BYTE reg) -> BYTE {
                    if (reg == 0) return B; if (reg == 1) return C; if (reg == 2) return D; if (reg == 3) return E;
                    if (reg == 4) return index_h; // Вместо H читаем старшую половинку индекса
                    if (reg == 5) return index_l; // Вместо L читаем младшую половинку индекса
                    if (reg == 7) return A;
                    return 0;
                    };

                // Одиночные операции INC/DEC над половинками регистров (0x24, 0x25, 0x2C, 0x2D)
                if ((subOp & 0xC7) == 0x04) {
                    BYTE r = (subOp >> 3) & 7;
                    if (r == 4) { INC_REG(index_h); } if (r == 5) { INC_REG(index_l); }
                }
                if ((subOp & 0xC7) == 0x05) {
                    BYTE r = (subOp >> 3) & 7;
                    if (r == 4) { DEC_REG(index_h); } if (r == 5) { DEC_REG(index_l); }
                }

                // Команды типа LD r, r' над половинками регистров
                if ((subOp & 0xC0) == 0x40) {
                    BYTE val = get_val(srcReg);
                    if (destReg == 0) B = val; else if (destReg == 1) C = val;
                    else if (destReg == 2) D = val; else if (destReg == 3) E = val;
                    else if (destReg == 4) index_h = val; else if (destReg == 5) index_l = val;
                    else if (destReg == 7) A = val;
                    return 4;
                }

                // Операции ALU над половинками регистров (ADD, SUB, AND, CP и т.д.)
                if ((subOp & 0xC0) == 0x80) {
                    BYTE val = get_val(srcReg);
                    switch ((subOp >> 3) & 7) {
                    case 0: ALU_ADD(val); break; case 1: ALU_ADC(val); break;
                    case 2: ALU_SUB(val); break; case 3: ALU_SBC(val); break;
                    case 4: ALU_AND(val); break; case 5: ALU_XOR(val); break;
                    case 6: ALU_OR(val); break;  case 7: ALU_CP(val); break;
                    }
                    return 4;
                }

                // Опкоды загрузки непосредственного значения типа LD IXH, n
                if (subOp == 0x26) { index_h = ReadMem(PC++); return 7; }
                if (subOp == 0x2E) { index_l = ReadMem(PC++); return 7; }
            }

            return 4;
        }

        default:
            if ((op & 0xC0) == 0x40 && op != 0x76) {
                BYTE srcIdx = op & 7; BYTE destIdx = (op >> 3) & 7; BYTE srcVal = 0;
                if (srcIdx == 0) srcVal = B; else if (srcIdx == 1) srcVal = C; else if (srcIdx == 2) srcVal = D; else if (srcIdx == 3) srcVal = E;
                else if (srcIdx == 4) srcVal = H; else if (srcIdx == 5) srcVal = L; else if (srcIdx == 6) srcVal = ReadMem(GetHL()); else if (srcIdx == 7) srcVal = A;

                if (destIdx == 0) B = srcVal; else if (destIdx == 1) C = srcVal; else if (destIdx == 2) D = srcVal; else if (destIdx == 3) E = srcVal;
                else if (destIdx == 4) H = srcVal; else if (destIdx == 5) L = srcVal; else if (destIdx == 6) WriteMem(GetHL(), srcVal); else if (destIdx == 7) A = srcVal;
                return (srcIdx == 6 || destIdx == 6) ? 7 : 4;
            }
            // ЖЕСТКИЙ АППАРАТНЫЙ ПЕРЕХВАТ НЕОПОЗНАННОЙ КОМАНДЫ
            {
                wchar_t errBuf[256];
                swprintf_s(errBuf, 256, L"КРИТИЧЕСКАЯ ОШИБКА: Обнаружена нереализованная команда Z80!\nОпкод: 0x%02X\nАдрес PC: 0x%04X", op, current_pc);
                MessageBoxW(NULL, errBuf, L"Аппаратный сбой процессора", MB_OK | MB_ICONERROR);
                emulator_running = false; // Останавливаем бесконечный цикл симулятора
                return 0;
            }
        }
    }

};
#pragma pack(pop)

CPUZ80 cpu;
static const int KeyAddressMap[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int id = GetWindowLongPtrW(hwnd, GWLP_ID) - 100;
    if (id >= 0 && id < 12) {
        int hw_address = KeyAddressMap[id];
        if (msg == WM_LBUTTONDOWN) { keys_pressed[hw_address] = true; cpu.UpdateHardwareState(); }
        else if (msg == WM_LBUTTONUP) { keys_pressed[hw_address] = false; cpu.UpdateHardwareState(); }
    }
    return CallWindowProcW(OldButtonProc, hwnd, msg, wParam, lParam);
}

bool LoadROM(const wchar_t* filename) {
    FILE* f = NULL;
    if (_wfopen_s(&f, filename, L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    loaded_rom_file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (loaded_rom_file_size <= 0) { fclose(f); return false; }

    fread(rom, 1, loaded_rom_file_size, f);
    fclose(f);
    rom_size = loaded_rom_file_size;
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hEditDisplay = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L" ",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER, 20, 20, 340, 40, hwnd, NULL, NULL, NULL);
        HFONT hFontDisp = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE,
            FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECISION, CLIP_DEFAULT_PRECISION,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Courier New");
        SendMessageW(hEditDisplay, WM_SETFONT, (WPARAM)hFontDisp, TRUE);
        hEditDebug = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE, 380, 20, 240, 310, hwnd, NULL, NULL, NULL);
        HFONT hFontDebug = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE,
            FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECISION, CLIP_DEFAULT_PRECISION,
            CLEARTYPE_QUALITY, FIXED_PITCH, L"Courier New");
        SendMessageW(hEditDebug, WM_SETFONT, (WPARAM)hFontDebug, TRUE);
        hEditRAM_Low = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | WS_VSCROLL, 20, 385, 600, 150, hwnd, NULL, NULL, NULL);
        SendMessageW(hEditRAM_Low, WM_SETFONT, (WPARAM)hFontDebug, TRUE);
        hEditRAM = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | WS_VSCROLL, 20, 550, 600, 120, hwnd, NULL, NULL, NULL);
        SendMessageW(hEditRAM, WM_SETFONT, (WPARAM)hFontDebug, TRUE);
        const wchar_t* keyLabels[] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"*", L"0", L"#" };
        int btnIndex = 0;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 3; ++c) {
                hBtnKeys[btnIndex] = CreateWindowW(L"BUTTON",
                    keyLabels[btnIndex], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20 + c * 113, 80 + r * 50, 100, 40, hwnd, (HMENU)(INT_PTR)(100 + btnIndex), NULL, NULL);
                if (OldButtonProc == NULL) OldButtonProc =
                    (WNDPROC)SetWindowLongPtrW(hBtnKeys[btnIndex], GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
                else SetWindowLongPtrW(hBtnKeys[btnIndex],
                    GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
                btnIndex++;
            }
        }
        HWND hCheckC1Init = CreateWindowW(L"BUTTON", L"Трубка поднята (Линия С1)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 285, 340, 20, hwnd, (HMENU)200, NULL, NULL);
        // Вместо BST_CHECKED ставим BST_UNCHECKED, чтобы при старте трубка лежала:
        SendMessageW(hCheckC1Init, BM_SETCHECK, BST_UNCHECKED, 0);
        CreateWindowW(L"BUTTON", L"Аппаратный сброс (RESET Z80)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 335, 340, 35, hwnd, (HMENU)202, NULL, NULL);
        timerId = SetTimer(hwnd, 1, 20, NULL);
        break;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        // Перехватываем клик по нашему чекбоксу трубки (ID = 200)
        if (wmId == 200) {
            // Сразу же синхронизируем состояние железа АОН, не дожидаясь таймера
            cpu.UpdateHardwareState();
        }
        else if (wmId == 202) {
            cpu.Reset(); last_read_key_char = L'-';
            for (int d = 0; d < 8; ++d) { display_digits[d] = ' '; display_latch[d] = ' '; digit_lifetime[d] = 0; }
            cpu.UpdateHardwareState();
            SetFocus(hwnd);
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == 1) {
            wchar_t debugBuf[1024]; // Увеличили буфер под расширенную отладку

            int final_direct_code = -1;
            for (int i = 0; i < 12; i++) {
                if (keys_pressed[i]) { final_direct_code = i; break; }
            }

            wchar_t gui_key_str[32];
            if (final_direct_code != -1) swprintf_s(gui_key_str, 32, L"0x%02X", final_direct_code);
            else swprintf_s(gui_key_str, 32, L"-");

            // Извлекаем текущие значения обратного отсчета для всех трех каналов DD8
            WORD ch0 = pit.channels[0].count_down;
            WORD ch1 = pit.channels[1].count_down;
            WORD ch2 = pit.channels[2].count_down;

            // --- ОБНОВЛЕННЫЙ ПАНЕЛЬНЫЙ ДИСПЛЕЙ ОТЛАДКИ ---
            swprintf_s(debugBuf, 1024,
                L"--- Z80 REGISTERS ---\r\nA: 0x%02X F: 0x%02X\r\nBC: 0x%02X%02X\r\nDE: 0x%02X%02X\r\nHL: 0x%02X%02X\r\nSP: 0x%04X PC: 0x%04X\r\nIX: 0x%04X IY: 0x%04X\r\n\r\n"
                L"--- STATE ---\r\nIM MODE: %d\r\nGUI_KEY: %s\r\n\r\n"
                L"--- PIT КР580ВИ53 ---\r\n"
                L"CH0 (Звук/Время): 0x%04X (Out: %s)\r\n"
                L"CH1 (Системный):  0x%04X\r\n"
                L"CH2 (АON-Прием):  0x%04X",
                cpu.A, cpu.F, cpu.B, cpu.C, cpu.D, cpu.E, cpu.H, cpu.L, cpu.SP, cpu.PC, cpu.IX, cpu.IY,
                cpu.IM, gui_key_str,
                ch0, vi53_out0_state ? L"HIGH" : L"LOW", ch1, ch2);
            SetWindowTextW(hEditDebug, debugBuf);

            // Плавное послесвечение сегментов
            for (int d = 0; d < 8; ++d) {
                if (digit_lifetime[d] > 0) digit_lifetime[d]--;
                else display_digits[d] = ' ';
            }

            wchar_t textBuf[64];
            swprintf_s(textBuf, 64, L"[ %c%c%c%c%c%c%c%c ]",
                display_digits[0], display_digits[1], display_digits[2], display_digits[3],
                display_digits[4], display_digits[5], display_digits[6], display_digits[7]);
            SetWindowTextW(hEditDisplay, textBuf);
        }
        break;
    }
    case WM_DESTROY: KillTimer(hwnd, timerId); PostQuitMessage(0); break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

const double PIT_CLOCK_HZ = 875000.0; // = SYSTEM_CLOCK_HZ / 4, см. aon_update() в эталоне
const double TICKS_PER_SAMPLE = 3500000.0 / (double)AUDIO_SAMPLE_RATE; // audio_ticks_accumulator считает такты Z80 (3.5 МГц), а не такты ВИ53 (875 кГц)!

void FillAudioBuffer(short* buffer, int samplesCount) {
    for (int i = 0; i < samplesCount; i++) {
        if (inte_ring_read_ptr != inte_ring_write_ptr) {
            buffer[i] = inte_ring_buffer[inte_ring_read_ptr];
            inte_ring_read_ptr = (inte_ring_read_ptr + 1) % INTE_RING_BUF_SIZE;
        }
        else {
            int inte_part = rk_inte_speaker_state ? 3000 : -3000;
            int port_c_part = rk_port_c_speaker_state ? 3000 : -3000;
            int vi53_part = vi53_out0_state ? 2000 : -2000;
            buffer[i] = (short)((inte_part + port_c_part + vi53_part) / 2);
        }
    }
}

void CALLBACK WaveOutCallback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        WAVEHDR* pHdr = (WAVEHDR*)dwParam1;
        FillAudioBuffer((short*)pHdr->lpData, AUDIO_BUF_SIZE);
        waveOutWrite(hwo, pHdr, sizeof(WAVEHDR));
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (!LoadROM(L"rom.bin")) {
        MessageBoxW(NULL, L"Файл прошивки ПЗУ Z80 'rom.bin' не найден!", L"Внимание", MB_OK | MB_ICONWARNING);
    }
    cpu.Reset();
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW); wc.lpfnWndProc = WndProc; wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"Z80AONSimulatorClass";
    if (!RegisterClassExW(&wc)) return 0;
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"Z80AONSimulatorClass", L"Симулятор АОН на базе Z80 / КР580ВИ53", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 660, 735, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow); UpdateWindow(hwnd);
    WAVEFORMATEX wfx = { 0 };
    wfx.wFormatTag = WAVE_FORMAT_PCM; wfx.nChannels = 1; wfx.nSamplesPerSec = AUDIO_SAMPLE_RATE; wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2; wfx.nAvgBytesPerSec = AUDIO_SAMPLE_RATE * 2;
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)WaveOutCallback, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
        for (int i = 0; i < 2; i++) {
            audioBuffers[i] = new short[AUDIO_BUF_SIZE];
            memset(audioBuffers[i], 0, AUDIO_BUF_SIZE * sizeof(short));
            waveHeader[i].lpData = (LPSTR)audioBuffers[i];
            waveHeader[i].dwBufferLength = AUDIO_BUF_SIZE * sizeof(short);
            waveHeader[i].dwFlags = 0;
            waveOutPrepareHeader(hWaveOut, &waveHeader[i], sizeof(WAVEHDR));
            waveOutWrite(hWaveOut, &waveHeader[i], sizeof(WAVEHDR));
        }
    }
    MSG msg;
    LARGE_INTEGER frequency, last_hardware_time;
    double internal_cycles_debt = 0.0;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&last_hardware_time);
    const double SYSTEM_CLOCK_HZ = 3500000.0;
    static double pit_ticks_accumulator = 0.0;
    static double audio_ticks_accumulator = 0.0;
    static bool last_ch0_out_state = true; // прерывание берётся с CT_0 ("Звук/Время"), не с CT_1

    while (emulator_running) {
        MsgWaitForMultipleObjectsEx(0, NULL, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { emulator_running = false; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!emulator_running) break;

        LARGE_INTEGER current_hardware_time;
        QueryPerformanceCounter(&current_hardware_time);
        double elapsed_seconds = (double)(current_hardware_time.QuadPart - last_hardware_time.QuadPart) / frequency.QuadPart;

        if (elapsed_seconds > 0.1) elapsed_seconds = 0.1;
        if (elapsed_seconds > 0.0) {
            last_hardware_time = current_hardware_time;
            internal_cycles_debt += elapsed_seconds * SYSTEM_CLOCK_HZ;
            int cycles_to_execute = (int)internal_cycles_debt;
            internal_cycles_debt -= cycles_to_execute;

            while (cycles_to_execute > 0) {
                int slice = (cycles_to_execute > 2000) ? 2000 : cycles_to_execute;
                cycles_to_execute -= slice;

                while (slice > 0) {
                    // 1. Процессор делает один шаг и возвращает количество потраченных тактов Z80
                    int elapsed_ticks = cpu.Step();
                    slice -= elapsed_ticks;
                    audio_ticks_accumulator += elapsed_ticks;

                    // 2. Масштабируем такты Z80 (3.5 МГц) в такты таймера КР580ВИ53 (0.85 МГц)
                    pit_ticks_accumulator += (double)elapsed_ticks * 0.25; // PIT_CLK = CPU_CLK / 2
                    if (pit_ticks_accumulator >= 1.0) {
                        int pit_ticks_to_update = (int)pit_ticks_accumulator;
                        pit_ticks_accumulator -= pit_ticks_to_update;

                        // Тактуем исправленный таймер пошагово для ловли фронтов прерываний
                        for (int p = 0; p < pit_ticks_to_update; p++) {
                            pit.channels[0].gate = true;                          // CT0 GATE -> +5В
                            pit.channels[1].gate = (cpu.ppi_pa & 0x40) != 0;      // CT1 GATE = PA6
                            pit.channels[2].gate = (cpu.ppi_pa & 0x80) != 0;      // CT2 GATE = PA7

                            bool prev_ch0_out = pit.channels[0].out_signal;

                            // Обновляем таймер ровно на 1 такт периферии
                            pit.Update(1);

                            // Маскируемое прерывание /INT генерируется строго по КАЖДОМУ изменению (фронту/спаду) OUT0
                            if (prev_ch0_out != pit.channels[0].out_signal) {
                                cpu.int_pending = true;
                            }
                        }
                    }

                    // 3. ПОЛНЫЙ БЛОК ОБРАБОТКИ АУДИО (Запрос из предыдущего шага)
                    while (audio_ticks_accumulator >= TICKS_PER_SAMPLE) {
                        audio_ticks_accumulator -= TICKS_PER_SAMPLE;

                        // Извлекаем состояние выхода Канала 0 ВИ53 (Звук)
                        // Вместо: vi53_out0_state = pit.channels[0].out_signal;
                        // Пишем правильную логику звука АОН:
                        if (cpu.ppi_pa & 0x10) { // PPI8255_PORTA, PIN_D4 (Звук)
                            vi53_out0_state = pit.channels[2].out_signal;
                        }
                        else {
                            vi53_out0_state = false; // Звук выключен прошивкой
                        }

                        int inte_part = rk_inte_speaker_state ? 3000 : -3000;
                        int port_c_part = rk_port_c_speaker_state ? 3000 : -3000;
                        int vi53_part = vi53_out0_state ? 2000 : -2000;

                        short audio_sample = (short)((inte_part + port_c_part + vi53_part) / 2);

                        int next_write = (inte_ring_write_ptr + 1) % INTE_RING_BUF_SIZE;
                        if (next_write != inte_ring_read_ptr) {
                            inte_ring_buffer[inte_ring_write_ptr] = audio_sample;
                            inte_ring_write_ptr = next_write;
                        }
                    }
                }
            }
        }
        if (internal_cycles_debt <= 0.0) Sleep(1);
    }
    if (hWaveOut) {
        for (int i = 0; i < 2; i++) { waveOutUnprepareHeader(hWaveOut, &waveHeader[i], sizeof(WAVEHDR)); delete[] audioBuffers[i]; }
        waveOutClose(hWaveOut);
    }
    return (int)msg.wParam;
}


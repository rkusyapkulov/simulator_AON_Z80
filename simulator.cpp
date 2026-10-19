#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define OUT_OUTLINE_PRECISION 8
#define CLIP_DEFAULT_PRECISION 0
#include <windows.h>
#include <stdint.h>

#pragma comment (lib, "libcmt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

// --- СОВМЕСТИМОСТЬ С СИ-СТРУКТУРОЙ ИЗ Z80.C ---
typedef uint8_t  u8;
typedef uint16_t u16;
typedef int8_t   i8;

extern "C" {
    // 1. Объявляем плоские Си-функции, которые реально существуют в z80.c
    void init(void);
    int step(void);
    void gen_nmi(void);
    void gen_int(u8 data);

    // 2. Объявляем структуру в точности такой, какая она в z80.c
    struct CPUZ80 {
        u16 PC, SP, IX, IY;
        u16 wz;
        u8 A, B, C, D, E, H, L;
        u8 A_alt, B_alt, C_alt, D_alt, E_alt, H_alt, L_alt, F_alt;
        u8 I, RR;
        u8 F;
        u8 iff_set;
        bool IFF1, IFF2;
        u8 IM;
        u8 int_vec;
        bool int_pending, nmi_pending;
        bool halted;

        // Вместо __thiscall методов делаем inline-вызовы плоских Си-функций.
        // Это сохраняет синтаксис cpu.step() и cpu.init() во всём C++ коде!
        inline void init() { ::init(); }
        inline int step() { return ::step(); }
        inline void gen_nmi() { ::gen_nmi(); }
        inline void gen_int(u8 data) { ::gen_int(data); }
    };

    // Глобальный объект процессора, выделенный в z80.c
    extern struct CPUZ80 cpu;
}

// Helper macros/functions updated to route through cpu.F and the new registers
static inline u16 GetHL() { return ((u16)cpu.H << 8) | cpu.L; }
static inline void SetHL(u16 val) { cpu.H = (u8)(val >> 8); cpu.L = (u8)(val & 0xFF); }
static inline u16 GetBC() { return ((u16)cpu.B << 8) | cpu.C; }
static inline void SetBC(u16 val) { cpu.B = (u8)(val >> 8); cpu.C = (u8)(val & 0xFF); }
static inline u16 GetDE() { return ((u16)cpu.D << 8) | cpu.E; }
static inline void SetDE(u16 val) { cpu.D = (u8)(val >> 8); cpu.E = (u8)(val & 0xFF); }

static inline u8 get_f() { return cpu.F; }
static inline void set_f(u8 val) { cpu.F = val; }

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

BYTE ppi_pa, ppi_pb, ppi_pc, ppi_ctrl; //

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

extern "C" u8 ReadByte(u16 addr) {
    if (addr & 0x2000)
    {
        return ram[addr & (RAM_SIZE - 1)];
    }
    else
    {
        addr = (addr & 0x1fff) | ((addr >> 1) & 0xe000);	//translate ROM address lines
        if (ppi_pa & 0x40) 
            addr |= 0x8000;	//second 32K bank
        return rom[addr & (rom_size - 1)];
    }
}

extern "C" void WriteByte(u16 addr, u8 value) {
    if (addr & 0x2000) ram[addr & (RAM_SIZE - 1)] = value;
}

// --- 100% ИСПРАВЛЕННАЯ СИНХРОННАЯ ФУНКЦИЯ ЧТЕНИЯ ИЗ ПОРТОВ ---
extern "C" u8 InPort(u16 port) {
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
extern "C" void OutPort(u16 port, u8 val) {
    // Вычисляем индекс регистра со смещением шины адреса
    BYTE reg = (port >> 3) & 3;

    // 1. Перехват порта вывода сегментов индикатора (строго по рабочему коду: if(!(adr&0x80)))
    if (!(port & 0x80)) {
        ir23_display_segments = val; // Маска палочек индикатора
        UpdateHardwareState();
        return;
    }

    // 2. Дешифрация портов КР580ВВ55А
    if (!(port & 0x20)) {
        switch (reg) {
        case 0: // Порт А: Изменение позиции индикатора
            ppi_pa = val;
            UpdateHardwareState();
            break;

        case 1: // Порт B
            ppi_pb = val;
            break;

        case 2: // Порт C: Звук и сканирование строк кнопок
            ppi_pc = val;
            // Синхронизация системного звука АОН
            rk_port_c_speaker_state = (val & 0x02) ? true : false;
            UpdateHardwareState();
            break;

        case 3: // Регистр Управляющего Слова (РУС)
            ppi_ctrl = val;
            UpdateHardwareState();
            break;
        }
        return;
    }

    // 3. Дешифрация КР580ВИ53
    if (!(port & 0x40)) {
        if (reg == 0x03) {
            pit.WriteControl(val);
        }
        else {
            pit.WriteData(reg, val);
        }
        return;
    }
}

void Reset() {
    cpu.init();
    // Аппаратный /RES сбрасывает и КР580ВВ55А — обязательно возвращаем
    // выбор банка ПЗУ (PA6) и остальные защёлки портов в исходное состояние,
    // иначе повторный Reset() с 64 КБ ПЗУ прочитает вектор сброса из "зависшего" банка.
    ppi_pa = 0;
    ppi_pb = 0;
    ppi_pc = 0;
    ppi_ctrl = 0;

    // Очищаем ОЗУ
    memset(ram, 0, 8192);
}

static const int KeyAddressMap[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int id = GetWindowLongPtrW(hwnd, GWLP_ID) - 100;
    if (id >= 0 && id < 12) {
        int hw_address = KeyAddressMap[id];
        if (msg == WM_LBUTTONDOWN) { keys_pressed[hw_address] = true; UpdateHardwareState(); }
        else if (msg == WM_LBUTTONUP) { keys_pressed[hw_address] = false; UpdateHardwareState(); }
    }
    return CallWindowProcW(OldButtonProc, hwnd, msg, wParam, lParam);
}

bool LoadROM(const wchar_t* filename) {
	// Открываем файл через Win32 API
	HANDLE hFile = CreateFileW(
		filename, 
		GENERIC_READ, 
		FILE_SHARE_READ, 
		NULL, 
		OPEN_EXISTING, 
		FILE_ATTRIBUTE_NORMAL, 
		NULL
	);
	
	if (hFile == INVALID_HANDLE_VALUE) return false;
	memset(rom, 0, ROM_SIZE);
	DWORD bytesRead = 0;
	// Читаем файл напрямую в буфер памяти
	BOOL success = ReadFile(hFile, rom, (DWORD)ROM_SIZE, &bytesRead, NULL);
	rom_size = bytesRead;
	
	CloseHandle(hFile);
	return success && (bytesRead > 0);
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
            UpdateHardwareState();
        }
        else if (wmId == 202) {
            Reset(); last_read_key_char = L'-';
            for (int d = 0; d < 8; ++d) { display_digits[d] = ' '; display_latch[d] = ' '; digit_lifetime[d] = 0; }
            UpdateHardwareState();
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
            if (final_direct_code != -1) wsprintfW(gui_key_str, L"0x%02X", final_direct_code);
            else wsprintfW(gui_key_str, L"-");

            // Извлекаем текущие значения обратного отсчета для всех трех каналов DD8
            WORD ch0 = pit.channels[0].count_down;
            WORD ch1 = pit.channels[1].count_down;
            WORD ch2 = pit.channels[2].count_down;

            // --- ОБНОВЛЕННЫЙ ПАНЕЛЬНЫЙ ДИСПЛЕЙ ОТЛАДКИ ---
            wsprintfW(debugBuf,
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
            wsprintfW(textBuf, L"[ %c%c%c%c%c%c%c%c ]",
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
    Reset();
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
                    int elapsed_ticks = cpu.step();
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
                            pit.channels[1].gate = (ppi_pa & 0x40) != 0;      // CT1 GATE = PA6
                            pit.channels[2].gate = (ppi_pa & 0x80) != 0;      // CT2 GATE = PA7

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
                        if (ppi_pa & 0x10) { // PPI8255_PORTA, PIN_D4 (Звук)
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


// ── СТЕНА ЦЕНЫ ЗАГОЛОВКА (наряд M-128) — ЗАМЕР, А НЕ ГРЕП ─────────────────
//
// ЗАКОН (AGENTS §5 п.13): тело в заголовке платит КАЖДЫЙ включивший, значит в
// заголовке живёт только то, чья цена O(1) по капам мира. Фатально СОВПАДЕНИЕ
// трёх условий: (1) тело в заголовке, (2) внутри именованная переменная С
// ИНИЦИАЛИЗАТОРОМ, (3) тип размером с кап.
//
// ПОЧЕМУ ЗАМЕР, А НЕ ГРЕП ТРЁХ УСЛОВИЙ (вердикт владельца 2026-09-26). Грепу
// недоступно условие (3): `sizeof` знает только компилятор, а список
// «капоносных типов» в скрипте — второй словарь, который разъедется с кодом. И
// главное: греп ловит ФОРМУ, которая уже известна. Здешний дефект держался на
// связке NSDMI + `make_unique`; следующий будет другим — `EvaluateAsInitializer`
// не единственный проход, умеющий обойти кап. Замер ловит ЦЕНУ, кто бы её ни
// устроил.
//
// ЧТО МЕРИТСЯ: пиковая память компилятора на TU из ОДНОЙ строки
// `#include "<заголовок>"` под `-fsyntax-only`, для каждого заголовка `src/**.h`.
// ПАМЯТЬ, а не время: время стенное и помнит загрузку машины (та же сборка
// гуляет в 3.26×), а пиковый RSS процесса от соседей не зависит. Замер
// 2026-09-26: больной `store.h` под `-fsyntax-only` — 64.99 с / 6096 МиБ, под
// `-c -O3` — 64.53 с / 5928 МиБ, то есть болезнь живёт во ФРОНТЕНДЕ и видна
// без кодогенерации; здоровый свод 179 заголовков стоит 19 с на трёх процессах.
//
// ПОЧЕМУ ЭТО ТЕСТ, А НЕ ОТДЕЛЬНЫЙ ТАРГЕТ: `check` — единственная честная дверь
// вердикта (§8 п.8), и вердикт этой стены детерминирован. Скачок ВРЕМЕНИ
// сборки судит прибор `tu_time_jump` — отдельным таргетом, ровно потому что
// стенное время детерминированным не бывает.
//
// МАШИНА НЕ УМИРАЕТ ПО ПОСТРОЕНИЮ. Больной TU просил footprint 28 ГиБ (замер
// ядра в JetsamEvent) — значит стена, которая его ЗАМЕРЯЕТ, обязана его же и
// оборвать. `RLIMIT_AS` для этого не годится: проверено 2026-09-26 — под
// `ulimit -v 2800000` больной заголовок всё равно дошёл до 6090 МиБ RSS (macOS
// не исполняет этот лимит для mmap). Поэтому предел держит РОДИТЕЛЬ: опрашивает
// живых детей и убивает того, кто перерос сетку.
//
// ГРАНИЦА СВОДА — `src/**.h`, то есть заголовки МИРА. `tests/*.h` сюда не
// входят: их платит сюита, а не игра.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/sysctl.h>
#endif

#include "check.h"

namespace {

// ── СТЕНА — ОТНОСИТЕЛЬНАЯ, И ОНА ВЫВЕДЕНА ИЗ ДВУХ ЗАМЕРЕННЫХ НАСЕЛЕНИЙ ────
// Перепись всех 179 заголовков `src/**.h` 2026-09-26: медиана 97 МиБ, худший
// ЗАКОННЫЙ — 271 МиБ (`app/app_state.h`), то есть 2.79× медианы. Тот же замер
// на больном `store.h` — 6096 МиБ, 62.8× медианы. Стена стоит в геометрической
// середине между законным максимумом и дефектом: √(2.79 × 62.8) = 13.2 — и
// взята на шаг в сторону дефекта, 12×, чтобы кусала раньше. Запас в обе
// стороны: 4.3× над худшим законным и 5.2× под замеренным дефектом. Абсолютные
// мегабайты писать нельзя: сменится clang или вырастет `ecs/components.h` — и
// константа станет ложью; медиана прогона переезжает вместе с деревом сама.
constexpr double kWallRatio = 12.0;

// Опрос живых детей. 50 мс — пятая часть самого дешёвого пробника (0.25 с), то
// есть даже он успевает быть опрошен; больной растёт ~100 МиБ/с (6 ГиБ за 60 с),
// значит за интервал прибавит ~5 МиБ — меньше процента сетки. Это МЕХАНИКА
// опроса, а не величина мира.
constexpr double kPollSec = 0.05;

// Контроль удваивает кап, пока стена не укусит. Восемь удвоений от 1 М ячеек —
// до 128 М ячеек (1 ГиБ данных): заведомо больше, чем нужно любой стене,
// замеренный `store.h` брал 6 ГиБ на 1.46 ГиБ данных.
constexpr int kMaxDoublings = 8;
constexpr unsigned long long kControlStartCells = 1ull << 20;

enum class Verdict { Ok, OverWall, DidNotCompile };

struct Probe {
    std::string label;   // "macro/store.h" или "control:sick"
    std::string tu;      // сгенерированный .cpp пробника
    std::string log;     // перехваченный вывод компилятора
    pid_t pid = -1;
    int status = -1;
    long long peakBytes = 0;
    double sec = 0.0;
    double startedAt = 0.0;
    bool killed = false;
    bool spawnFailed = false;
};

// Компилятор и его аргументы — ровно те, чем собирается игра; CMake пишет их
// файлом из ТЕХ ЖЕ таргетов (§9: флаги живут в одном месте).
struct Toolchain {
    std::string cxx;
    std::vector<std::string> args;
};

double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

long long physical_memory_bytes() {
#ifdef __APPLE__
    std::int64_t mem = 0;
    std::size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 && mem > 0)
        return static_cast<long long>(mem);
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) return static_cast<long long>(pages) * static_cast<long long>(pageSize);
#endif
    return 0;
}

int core_count() {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? int(n) : 1;
}

// Пиковый RSS РЕБЁНКА, как его отдаёт ядро при пожинании. На Darwin `ru_maxrss`
// в БАЙТАХ, в Linux — в килобайтах; вердикт от единицы не зависит (он
// относительный), а напечатанные мегабайты — зависят.
long long reaped_peak_bytes(const struct rusage& ru) {
#ifdef __APPLE__
    return static_cast<long long>(ru.ru_maxrss);
#else
    return static_cast<long long>(ru.ru_maxrss) * 1024;
#endif
}

// Текущий RSS живого ребёнка — для сетки, не для вердикта.
long long resident_bytes(pid_t pid) {
#ifdef __APPLE__
    struct rusage_info_v2 ri;
    std::memset(&ri, 0, sizeof(ri));
    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t*)&ri) == 0)
        return static_cast<long long>(ri.ri_resident_size);
    return 0;
#else
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/statm", int(pid));
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    long long total = 0;
    long long resident = 0;
    const int got = std::fscanf(f, "%lld %lld", &total, &resident);
    std::fclose(f);
    if (got != 2) return 0;
    return resident * static_cast<long long>(sysconf(_SC_PAGESIZE));
#endif
}

bool write_text(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t wrote = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return wrote == text.size();
}

bool read_lines(const std::string& path, std::vector<std::string>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string all;
    char buf[65536];
    std::size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, got);
    std::fclose(f);
    std::size_t pos = 0;
    while (pos < all.size()) {
        std::size_t eol = all.find('\n', pos);
        if (eol == std::string::npos) eol = all.size();
        if (eol > pos) out.push_back(all.substr(pos, eol - pos));
        pos = eol + 1;
    }
    return true;
}

// Имя файла пробника. ПОРЯДКОВЫЙ НОМЕР впереди не украшение: без него
// `a/b.h` и `a_b.h` дали бы одно имя, один пробник затёр бы другой, и стена
// молча замерила бы чужой заголовок вместо своего. Сегодня столкновений нет
// (179 из 179 имён различны), и номер делает их невозможными впредь.
std::string probe_name(std::size_t index, const std::string& header) {
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%03zu_", index);
    std::string s = prefix + header;
    for (char& c : s)
        if (c == '/' || c == '\\' || c == '.') c = '_';
    return s;
}

pid_t spawn_probe(const Toolchain& tc, const Probe& p) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(tc.cxx.c_str()));
    for (const std::string& a : tc.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(const_cast<char*>(p.tu.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid != 0) return pid;

    // Своя группа процессов: оборвать надо ВЕСЬ пробник, а не одного его
    // представителя (драйвер компилятора умеет форкать cc1 — см. `run_probes`).
    setpgid(0, 0);
    const int fd = open(p.log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    execv(tc.cxx.c_str(), argv.data());
    _exit(127);
}

// Гоняет пробники по `jobs` за раз и обрывает того, кто перерос сетку.
// `netBytes` выведен из ФИЗИЧЕСКОЙ памяти машины, поэтому свод не умеет загнать
// её в своп, сколько бы больных заголовков он ни нашёл.
//
// ПОЧЕМУ ПРОБНИК ЗОВЁТСЯ С `-fintegrated-cc1` (замер 2026-09-26, дефект моей же
// сетки, пойманный боевым контролем). Драйвер `clang++` форкает `cc1`, и ВСЮ
// память держит внук: `ps` на больном заголовке показывал драйвер 14 МиБ и
// ребёнка 390 МиБ и выше. Вердикт от этого не страдал — `wait4` складывает
// усилия пожатых потомков, и пик 5647 МиБ он вернул честно, — а вот СЕТКА
// слепла: родитель опрашивал драйвер, видел 14 МиБ и не обрывал никого, так что
// больной свод шёл 550 с и доходил до 5.6 ГиБ. `proc_listchildpids` внука не
// отдаёт (возвращает ноль на собственного же потомка), поэтому лечение — не
// подсматривать за внуком, а не рождать его: с этим флагом cc1 живёт В ТОМ ЖЕ
// процессе, и опрос видит ровно ту память, которую меряет вердикт.
void run_probes(const Toolchain& tc, std::vector<Probe>& probes, int jobs,
                long long netBytes) {
    std::size_t next = 0;
    int inflight = 0;
    while (next < probes.size() || inflight > 0) {
        while (inflight < jobs && next < probes.size()) {
            Probe& p = probes[next];
            ++next;
            p.startedAt = now_sec();
            p.pid = spawn_probe(tc, p);
            if (p.pid < 0) {
                p.spawnFailed = true;
                p.pid = -1;
                continue;
            }
            ++inflight;
        }
        if (inflight == 0) continue;
        // Пожинаем без блокировки: блокирующий wait не дал бы сетке сработать.
        for (;;) {
            int status = 0;
            struct rusage ru;
            std::memset(&ru, 0, sizeof(ru));
            const pid_t pid = wait4(-1, &status, WNOHANG, &ru);
            if (pid <= 0) break;
            for (Probe& p : probes) {
                if (p.pid != pid) continue;
                p.status = status;
                p.peakBytes = reaped_peak_bytes(ru);
                p.sec = now_sec() - p.startedAt;
                p.pid = -1;
                break;
            }
            --inflight;
        }
        if (inflight == 0) continue;
        for (Probe& p : probes) {
            if (p.pid <= 0 || p.killed) continue;
            if (resident_bytes(p.pid) > netBytes) {
                killpg(p.pid, SIGKILL);  // лидер группы — сам пробник
                kill(p.pid, SIGKILL);
                p.killed = true;
            }
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = long(kPollSec * 1e9);
        nanosleep(&ts, nullptr);
    }
}

Verdict verdict_of(const Probe& p, double wallBytes) {
    // Порядок важен: убитый сеткой выходит с ненулевым статусом, но грех его —
    // ПАМЯТЬ, и называться он должен так.
    if (double(p.peakBytes) > wallBytes) return Verdict::OverWall;
    if (p.spawnFailed) return Verdict::DidNotCompile;
    if (!WIFEXITED(p.status) || WEXITSTATUS(p.status) != 0) return Verdict::DidNotCompile;
    return Verdict::Ok;
}

double mib(long long bytes) { return double(bytes) / 1048576.0; }

void print_compiler_output(const Probe& p) {
    std::vector<std::string> lines;
    if (!read_lines(p.log, lines)) return;
    const std::size_t show = (lines.size() < 12u) ? lines.size() : 12u;
    for (std::size_t i = 0; i < show; ++i)
        std::fprintf(stderr, "      | %s\n", lines[i].c_str());
}

// Синтетический заголовок контроля: ТРИ УСЛОВИЯ ЗАКОНА, собранные нарочно.
// `withInitializer` переключает ровно одно — второе, — при неизменных байтах
// типа. Так контроль доказывает, что стена кусает ФОРМУ, а не размер: замер
// 2026-09-26 на 4 М ячеек — 728 МиБ с инициализатором против 53 МиБ без.
std::string synthetic_header(unsigned long long cells, bool withInitializer) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "#pragma once\n"
                  "#include <array>\n"
                  "#include <cstdint>\n"
                  "#include <memory>\n"
                  "namespace hdrwall {\n"
                  "struct Cell { std::uint64_t v = 0; };\n"
                  "struct Cap { std::array<Cell, %lluull> col; };\n"
                  "inline std::unique_ptr<Cap> make_cap() {\n"
                  "%s"
                  "    return s;\n"
                  "}\n"
                  "}\n",
                  cells,
                  withInitializer
                      ? "    auto s = std::make_unique<Cap>();\n"
                      : "    std::unique_ptr<Cap> s;\n    s.reset(new Cap());\n");
    return std::string(buf);
}

// Принимает ли компилятор флаг — СПРАШИВАЕМ У НЕГО, а не у своей памяти о том,
// какой это компилятор. `-fintegrated-cc1` знает clang и не знает gcc; стена
// обязана работать у обоих, просто у второго сетка грубее, и она об этом
// говорит вслух.
bool compiler_takes(const Toolchain& probe, const std::string& probeDir,
                    const std::string& flag) {
    Toolchain tc = probe;
    tc.args.push_back(flag);
    Probe p;
    p.label = "flag:" + flag;
    p.tu = probeDir + "/flag_probe.cpp";
    p.log = probeDir + "/flag_probe.out";
    if (!write_text(p.tu, "int main() { return 0; }\n")) return false;
    std::vector<Probe> one{p};
    run_probes(tc, one, 1, /*netBytes=*/0x7fffffffffffffffLL);
    return WIFEXITED(one[0].status) && WEXITSTATUS(one[0].status) == 0;
}

// Один пробник, один прогон — для контролей, которым сетка машины служит
// последним предохранителем.
Probe run_one(const Toolchain& tc, const std::string& label, const std::string& tuPath,
              const std::string& logPath, const std::string& includeAbs,
              long long netBytes) {
    Probe p;
    p.label = label;
    p.tu = tuPath;
    p.log = logPath;
    if (!write_text(p.tu, "#include \"" + includeAbs + "\"\n")) {
        p.spawnFailed = true;
        return p;
    }
    std::vector<Probe> one{p};
    run_probes(tc, one, 1, netBytes);
    return one[0];
}

void run_wall() {
    const std::string buildDir = TIMAERT_BUILD_DIR;
    const std::string probeDir = buildDir + "/header_probe";
    mkdir(probeDir.c_str(), 0755);

    Toolchain tc;
    tc.cxx = TIMAERT_CXX;
    CHECK_OR_RETURN(read_lines(buildDir + "/generated/header_probe.args", tc.args),
                    "аргументы пробника CMake пишет из ТЕХ ЖЕ таргетов, чем "
                    "собирается игра — файл обязан быть на месте");
    CHECK_OR_RETURN(!tc.args.empty(), "аргументы пробника не пусты");
    tc.args.push_back("-fsyntax-only");
    const bool integrated = compiler_takes(tc, probeDir, "-fintegrated-cc1");
    if (integrated) tc.args.push_back("-fintegrated-cc1");

    std::vector<std::string> headers;
    CHECK_OR_RETURN(read_lines(buildDir + "/generated/header_probe_list.txt", headers),
                    "список заголовков CMake пишет глобом src/**.h — файл "
                    "обязан быть на месте");
    CHECK_OR_RETURN(!headers.empty(), "в src/ есть заголовки, и список не пуст");

    const long long memBytes = physical_memory_bytes();
    CHECK_OR_RETURN(memBytes > 0, "физическая память машины прочитана");
    // Ширина — от ЯДЕР (четверть, чтобы свод не отнимал машину), сетка — от
    // ПАМЯТИ: jobs × сетка = половина физической памяти ПО ПОСТРОЕНИЮ, значит
    // свод не умеет её засвопить. Больной заголовок здесь не «медленный» — он
    // оборванный и названный.
    const int jobs = std::max(1, core_count() / 4);
    const long long netBytes = memBytes / (2 * jobs);

    std::vector<Probe> probes;
    probes.reserve(headers.size());
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const std::string& h = headers[i];
        Probe p;
        p.label = h;
        p.tu = probeDir + "/" + probe_name(i, h) + ".cpp";
        p.log = probeDir + "/" + probe_name(i, h) + ".out";
        if (!write_text(p.tu, "#include \"" + h + "\"\n")) continue;
        probes.push_back(p);
    }
    CHECK_OR_RETURN(probes.size() == headers.size(),
                    "пробник записан для КАЖДОГО заголовка — незамеренный "
                    "заголовок это дыра, а не «пропуск»");

    const double t0 = now_sec();
    run_probes(tc, probes, jobs, netBytes);
    const double sweepSec = now_sec() - t0;

    std::vector<long long> peaks;
    peaks.reserve(probes.size());
    for (const Probe& p : probes) peaks.push_back(p.peakBytes);
    std::sort(peaks.begin(), peaks.end());
    const double medianBytes = double(peaks[peaks.size() / 2]);
    const double wallBytes = kWallRatio * medianBytes;

    CHECK(medianBytes > 0.0,
          "медиана прогона положительна — иначе мерили не компилятор");
    CHECK(wallBytes < double(netBytes),
          "сетка машины выше стены: иначе оборванный сеткой заголовок судился "
          "бы железом, а не стеной");

    std::printf("  свод: %d заголовков за %.1f с (%d процесса, сетка %.0f МиБ%s)\n",
                int(probes.size()), sweepSec, jobs, mib(netBytes),
                integrated ? "" : ", БЕЗ -fintegrated-cc1: сетка видит только "
                                  "драйвер, пробник может перерасти её");
    std::printf("  медиана %.0f МиБ, стена %.0f МиБ (%.0f× медианы)\n",
                mib(static_cast<long long>(medianBytes)), mib(static_cast<long long>(wallBytes)), kWallRatio);

    // Пятёрка самых дорогих — чтобы приближение к стене замечали до красноты.
    std::vector<const Probe*> byPeak;
    byPeak.reserve(probes.size());
    for (const Probe& p : probes) byPeak.push_back(&p);
    std::sort(byPeak.begin(), byPeak.end(),
              [](const Probe* a, const Probe* b) { return a->peakBytes > b->peakBytes; });
    const std::size_t show = (byPeak.size() < 5u) ? byPeak.size() : 5u;
    for (std::size_t i = 0; i < show; ++i)
        std::printf("    %6.0f МиБ (%4.1f× медианы) %5.2f с  %s\n",
                    mib(byPeak[i]->peakBytes),
                    double(byPeak[i]->peakBytes) / medianBytes, byPeak[i]->sec,
                    byPeak[i]->label.c_str());

    int overWall = 0;
    int didNotCompile = 0;
    for (const Probe& p : probes) {
        const Verdict v = verdict_of(p, wallBytes);
        if (v == Verdict::Ok) continue;
        if (v == Verdict::OverWall) {
            ++overWall;
            std::fprintf(stderr,
                         "    СТЕНА: %s — %.0f МиБ, это %.1f× медианы прогона "
                         "(предел %.0f×)%s\n",
                         p.label.c_str(), mib(p.peakBytes),
                         double(p.peakBytes) / medianBytes, kWallRatio,
                         p.killed ? ", оборван сеткой" : "");
        } else {
            ++didNotCompile;
            std::fprintf(stderr, "    НЕ СОБИРАЕТСЯ САМ ПО СЕБЕ: %s\n", p.label.c_str());
            print_compiler_output(p);
        }
    }

    CHECK(overWall == 0,
          "ни один заголовок не дороже стены: капозависимое тело живёт в .cpp, "
          "а не в заголовке (AGENTS §5 п.13)");
    // Заголовок, который не собирается сам по себе, стеной НЕ ЗАМЕРЯЕТСЯ —
    // значит это дыра в измерении, а не мелочь стиля. Лечится строкой include
    // (§12: заголовок несёт то, чем пользуется).
    CHECK(didNotCompile == 0,
          "каждый заголовок собирается сам по себе — иначе он не замерен");

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ (§8 п.6): СТЕНА ОБЯЗАНА КУСАТЬ ────────────────
    // Не «синтетика на 4 М ячеек», а УДВОЕНИЕ КАПА, пока стена не сработает:
    // так контроль не зависит от подогнанного числа и заодно показывает то, что
    // закон утверждает, — цена пропорциональна КАПУ.
    const std::string sickHeader = probeDir + "/control_sick.h";
    unsigned long long cells = kControlStartCells;
    int doublings = 0;
    bool bit = false;
    long long sickPeak = 0;
    for (; doublings < kMaxDoublings; ++doublings, cells *= 2) {
        // Данные контроля сами по себе не должны перерасти сетку: кусать
        // обязана СТЕНА, а не железо.
        if (double(cells) * 8.0 > double(netBytes) / 4.0) break;
        if (!write_text(sickHeader, synthetic_header(cells, true))) break;
        const Probe p = run_one(tc, "control:sick", probeDir + "/control_sick.cpp",
                                probeDir + "/control_sick.out", sickHeader, netBytes);
        sickPeak = p.peakBytes;
        if (verdict_of(p, wallBytes) != Verdict::Ok) {
            bit = true;
            break;
        }
    }
    std::printf("  контроль больной: %llu ячеек (%d удвоений) — %.0f МиБ, %.1f× медианы\n",
                cells, doublings, mib(sickPeak), double(sickPeak) / medianBytes);
    CHECK(bit,
          "СТЕНА КУСАЕТ: именованная переменная с инициализатором над типом "
          "размером с кап пробивает предел — детектор проверен, а не обещан");

    // И контроль, ровно противоположный: ТЕ ЖЕ БАЙТЫ без именованного
    // инициализатора обязаны пройти. Иначе стена ловила бы просто «большой тип»
    // и запрещала бы законную преаллокацию мира.
    const std::string curedHeader = probeDir + "/control_cured.h";
    CHECK_OR_RETURN(write_text(curedHeader, synthetic_header(cells, false)),
                    "вылеченный контроль записан");
    const Probe cured = run_one(tc, "control:cured", probeDir + "/control_cured.cpp",
                                probeDir + "/control_cured.out", curedHeader, netBytes);
    std::printf("  контроль вылеченный: те же %llu ячеек — %.0f МиБ, %.1f× медианы\n",
                cells, mib(cured.peakBytes), double(cured.peakBytes) / medianBytes);
    if (verdict_of(cured, wallBytes) != Verdict::Ok) print_compiler_output(cured);
    CHECK(verdict_of(cured, wallBytes) == Verdict::Ok,
          "ТЕ ЖЕ БАЙТЫ без именованного инициализатора стену не задевают — "
          "стена судит ФОРМУ ТЕЛА, а не размер типа");
    CHECK(cured.peakBytes * 4 < sickPeak,
          "разрыв между формами кратный — именно он и есть дискриминатор");
}

} // namespace

int main() {
    run_wall();
    return sm::test::report("header_cost_test");
}

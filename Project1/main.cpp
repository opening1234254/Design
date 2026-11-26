// 单文件 wxWidgets 逻辑电路编辑器：界面、仿真、保存读取和 KiCad 导出均集中在本文件。
// 文件编码：UTF-8 带 BOM，行尾：Windows CRLF；中文界面字符串继续使用 Unicode 转义。
// 维护约定：修改功能、引脚定义、算法或导出行为时，必须同步更新相关中文注释，删除失效说明。
// 注释描述当前实现与约束；不要将待实现功能写成已经支持的功能。
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objbase.h>
#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/dcbuffer.h>
#include <wx/popupwin.h>
#include <wx/display.h>
#include <wx/tglbtn.h>
#include <wx/bmpbndl.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <iomanip>
#include <locale>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")


#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

// 界面与几何参数：元件和导线吸附到网格，方向编号依次为右、下、左、上。
constexpr int Grid = 20;


constexpr COLORREF Blue = RGB(22, 132, 232);
constexpr COLORREF Dark = RGB(39, 49, 59);


constexpr int DirectionX[4] = {1, 0, -1, 0};
constexpr int DirectionY[4] = {0, 1, 0, -1};
constexpr wchar_t const* DirectionNames[4] = {L"\u53F3", L"\u4E0B", L"\u5DE6", L"\u4E0A"};
// 元件显示名称、文件格式键名及 Kind 枚举必须保持相同顺序；新增元件时应同步维护。
constexpr wchar_t const* KindNames[] = {L"\u4E0E\u95E8", L"\u6216\u95E8", L"\u975E\u95E8", L"\u8F93\u5165\u5F15\u811A", L"\u8F93\u51FA\u5F15\u811A",
    L"\u5F02\u6216\u95E8", L"\u4E0E\u975E\u95E8", L"\u6216\u975E\u95E8", L"\u540C\u6216\u95E8", L"\u534A\u52A0\u5668", L"\u534A\u51CF\u5668", L"\u76F8\u7B49\u6BD4\u8F83\u5668", L"D \u89E6\u53D1\u5668", L"RS \u9501\u5B58\u5668", L"\u65F6\u949F\uFF08\u624B\u52A8\uFF09", L"\u903B\u8F91\u5E38\u91CF"};
constexpr char const* KindKeys[] = {"and", "or", "not", "input", "output",
    "xor", "nand", "nor", "xnor", "half_adder", "half_subtractor", "equal", "d_flipflop", "rs_latch", "clock", "constant"};

enum Kind { And, Or, Not, Input, Output, Xor, Nand, Nor, Xnor, HalfAdder, HalfSubtractor, Equal, DFlipFlop, RSLatch, Clock, Constant };
enum Tool { SelectTool, WireTool, AndTool, OrTool, NotTool, InputTool, OutputTool,
            XorTool, NandTool, NorTool, XnorTool, HalfAdderTool, HalfSubtractorTool, EqualTool, DFlipFlopTool, RSLatchTool, ClockTool, ConstantTool };

// 画布中的逻辑元件。value 对信号源表示电平，对触发器表示保存的 Q 状态。
struct Component {
    int id = 0;
    Kind kind = And;
    int x = 0, y = 0, direction = 0, inputs = 2;
    bool value = false;
    bool lastClock = false; // D 触发器上次观察到的时钟电平，保存后用于避免重复采样。
};

// 逻辑连接以元件编号和端口编号为准；bends、parentWire 和 tap 只描述折点与分支显示。
struct Wire {
    int id = 0;
    int source = 0, target = 0, port = 0;
    std::vector<POINT> bends;
    int parentWire = 0;
    POINT tap{};
    int sourcePort = -1;
};

struct Port {
    int component = 0;
    int index = -1; // -1 为主输出，-2 为第二输出；0..7 为输入。不要与物理焊盘编号混淆。
    POINT point{};
};

// 可保存、可撤销的电路模型；编号计数器用于为新元件和导线分配标识。
struct Circuit {
    std::vector<Component> components;
    std::vector<Wire> wires;
    int nextComponent = 1;
    int nextWire = 1;
};

bool same(POINT a, POINT b) { return a.x == b.x && a.y == b.y; }
int snap(int value) { return static_cast<int>(std::round(static_cast<double>(value) / Grid)) * Grid; }
POINT pt(int x, int y) { return POINT{x, y}; }
bool is_source(Kind kind) { return kind == Input || kind == Clock || kind == Constant; }
bool sequential(Kind kind) { return kind == DFlipFlop || kind == RSLatch; }
// 逻辑输入端口数：D 触发器为 D/CLK/CLR/SET（旧 CLR 索引不变）；RS 为 S/R。信号源的实际端口由 ports 特殊处理。
int input_count(Component const& c) {
    if (c.kind == DFlipFlop) return 4;
    return c.kind == Not || c.kind == Output ? 1 :
           c.kind >= HalfAdder ? 2 : c.inputs;
}
int output_count(Component const& c) {
    return c.kind == HalfAdder || c.kind == HalfSubtractor || sequential(c.kind) ? 2 : 1;
}
bool adjustable_gate(Kind kind) { return kind == And || kind == Or || kind == Xor ||
    kind == Nand || kind == Nor || kind == Xnor; }
bool inverted_gate(Kind kind) { return kind == Not || kind == Nand || kind == Nor || kind == Xnor; }

std::vector<int> input_offsets(int count) {
    std::vector<int> offsets;
    if (count % 2) {
        for (int i = -(count / 2); i <= count / 2; ++i) offsets.push_back(i * Grid);
    } else {
        for (int i = -(count / 2); i < 0; ++i) offsets.push_back(i * Grid);
        for (int i = 1; i <= count / 2; ++i) offsets.push_back(i * Grid);
    }
    return offsets;
}

int half_height(Component const& c) {
    if (sequential(c.kind)) return 80;
    int result = 23;
    for (int offset : input_offsets(input_count(c))) result = std::max(result, std::abs(offset) + 12);
    return result;
}

// 统一生成端口的位置和逻辑索引，供连线、命中检测、绘制及导出使用。D/CLK 在左、SET/CLR 在上下、Q/反相 Q 在右；保留旧 CLR 索引 2，新增 SET 索引 3。
std::vector<Port> ports(Component const& c) {
    std::vector<Port> result;
    int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
    if (sequential(c.kind)) {
        auto add = [&](int index, int x, int y) {
            result.push_back({c.id,index,pt(c.x+x*dx-y*dy,c.y+x*dy+y*dx)});
        };
        add(0,-80,-40); add(1,-80,c.kind == DFlipFlop ? 20 : 40);
        if (c.kind == DFlipFlop) { add(2,0,100); add(3,0,-100); }
        add(-1,80,-40); add(-2,80,40);
    } else if (is_source(c.kind)) {
        result.push_back({c.id, -1, pt(c.x + 40 * dx, c.y + 40 * dy)});
    } else if (c.kind == Output) {
        result.push_back({c.id, 0, pt(c.x - 40 * dx, c.y - 40 * dy)});
    } else {
        auto offsets = input_offsets(input_count(c));
        if (sequential(c.kind)) for (auto& offset : offsets) offset *= 2;
        for (int i = 0; i < static_cast<int>(offsets.size()); ++i) {
            result.push_back({c.id, i, pt(c.x - 40 * dx - offsets[i] * dy,
                                           c.y - 40 * dy + offsets[i] * dx)});
        }
        for (int i = 0; i < output_count(c); ++i) {
            int offset = output_count(c) == 1 ? 0 : (i == 0 ? -Grid : Grid);
            if (sequential(c.kind)) offset *= 2;
            result.push_back({c.id, -1 - i, pt(c.x + 40 * dx - offset * dy,
                                               c.y + 40 * dy + offset * dx)});
        }
    }
    return result;
}

Component* find_component(Circuit& circuit, int id) {
    for (auto& component : circuit.components) if (component.id == id) return &component;
    return nullptr;
}
Component const* find_component(Circuit const& circuit, int id) {
    for (auto const& component : circuit.components) if (component.id == id) return &component;
    return nullptr;
}
Wire const* find_wire(Circuit const& circuit, int id) {
    for (auto const& wire : circuit.wires) if (wire.id == id) return &wire;
    return nullptr;
}
Wire* find_wire(Circuit& circuit, int id) {
    for (auto& wire : circuit.wires) if (wire.id == id) return &wire;
    return nullptr;
}
POINT port_point(Component const& component, int index) {
    for (auto const& port : ports(component)) if (port.index == index) return port.point;
    return pt(component.x, component.y);
}

RECT component_bounds(Component const& c) {
    if (is_source(c.kind) || c.kind == Output) return RECT{c.x - 20, c.y - 20, c.x + 20, c.y + 20};
    int h = half_height(c);
    int w = sequential(c.kind) ? 60 : 23;
    int hx = c.direction % 2 == 0 ? w : h;
    int hy = c.direction % 2 == 0 ? h : w;
    return RECT{c.x - hx, c.y - hy, c.x + hx, c.y + hy};
}

bool intersects(RECT a, RECT b) {
    return a.left <= b.right && a.right >= b.left && a.top <= b.bottom && a.bottom >= b.top;
}

bool blocked(POINT p, std::vector<Component> const& components) {
    for (auto const& c : components) {
        if (is_source(c.kind) || c.kind == Output) {
            int x = p.x - c.x, y = p.y - c.y;
            if (x * x + y * y <= 400) return true;
        } else {
            int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
            int forward = std::abs((p.x - c.x) * dx + (p.y - c.y) * dy);
            int across = std::abs((p.x - c.x) * -dy + (p.y - c.y) * dx);
            if (forward <= (sequential(c.kind) ? 60 : 20) && across <= half_height(c)) return true;
        }
    }
    return false;
}

using RouteState = std::tuple<int, int, int>;
struct RouteQueueItem {
    int score, cost;
    RouteState state;
    bool operator<(RouteQueueItem const& rhs) const { return score > rhs.score; }
};

// 网格寻路：绕开元件，考虑转弯与首尾方向；生成正交导线。这里仅处理画布走线，不是 PCB 自动布线。
std::vector<POINT> grid_route(POINT start, POINT end, std::vector<Component> const& components,
                              int firstDirection = -1, int lastDirection = -1) {
    if (same(start, end)) return {start};
    int minX = std::min(start.x, end.x), maxX = std::max(start.x, end.x);
    int minY = std::min(start.y, end.y), maxY = std::max(start.y, end.y);
    for (auto const& c : components) {
        minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
    }
    minX -= 200; maxX += 200; minY -= 200; maxY += 200;
    RouteState origin = {start.x, start.y, -1};
    std::map<RouteState, int> best;
    std::map<RouteState, RouteState> previous;
    std::priority_queue<RouteQueueItem> queue;
    best[origin] = 0;
    queue.push({0, 0, origin});
    while (!queue.empty()) {
        RouteQueueItem item = queue.top(); queue.pop();
        if (item.cost != best[item.state]) continue;
        int x = std::get<0>(item.state), y = std::get<1>(item.state);
        int oldDirection = std::get<2>(item.state);
        if (x == end.x && y == end.y) {
            std::vector<POINT> result;
            RouteState state = item.state;
            for (;;) {
                result.push_back(pt(std::get<0>(state), std::get<1>(state)));
                if (state == origin) break;
                state = previous[state];
            }
            std::reverse(result.begin(), result.end());
            return result;
        }
        for (int direction = 0; direction < 4; ++direction) {
            if (oldDirection == -1 && firstDirection >= 0 && direction != firstDirection) continue;
            POINT next = pt(x + Grid * DirectionX[direction], y + Grid * DirectionY[direction]);
            if (next.x < minX || next.x > maxX || next.y < minY || next.y > maxY) continue;
            if (same(next, start)) continue;
            bool atEnd = same(next, end);
            if (atEnd && lastDirection >= 0 && direction != lastDirection) continue;
            if (!atEnd && blocked(next, components)) continue;
            RouteState state = {next.x, next.y, direction};
            int cost = item.cost + 10 + (oldDirection >= 0 && oldDirection != direction ? 3 : 0);
            auto found = best.find(state);
            if (found != best.end() && cost >= found->second) continue;
            best[state] = cost;
            previous[state] = item.state;
            int heuristic = (std::abs(next.x - end.x) + std::abs(next.y - end.y)) / Grid * 10;
            queue.push({cost + heuristic, cost, state});
        }
    }
    return {};
}

std::vector<POINT> route_points(Circuit const& circuit, Component const& source,
                                Component const* target, int inputPort,
                                std::vector<POINT> const& bends, POINT const* hover = nullptr,
                                POINT const* branchStart = nullptr, int sourcePort = -1) {
    POINT start = branchStart ? *branchStart : port_point(source, sourcePort);
    POINT end{};
    bool hasEnd = target != nullptr;
    if (hasEnd) end = port_point(*target, inputPort);
    std::vector<POINT> destinations;
    for (POINT bend : bends) {
        bend = pt(snap(bend.x), snap(bend.y));
        bool repeated = same(bend, start);
        for (auto const& prior : destinations) repeated = repeated || same(bend, prior);
        if (!repeated && !blocked(bend, circuit.components) && (!hasEnd || !same(bend, end))) destinations.push_back(bend);
    }
    if (hasEnd) destinations.push_back(end);
    else if (hover) destinations.push_back(pt(snap(hover->x), snap(hover->y)));
    std::vector<POINT> result{start};
    for (size_t i = 0; i < destinations.size(); ++i) {
        auto leg = grid_route(result.back(), destinations[i], circuit.components,
                              i == 0 && !branchStart ? source.direction : -1,
                              hasEnd && i + 1 == destinations.size() ?
                              (target->direction + (target->kind == DFlipFlop && inputPort == 2 ? 3 : target->kind == DFlipFlop && inputPort == 3 ? 1 : 0)) % 4 : -1);
        if (leg.empty()) {
            if (!bends.empty()) return route_points(circuit, source, target, inputPort, {}, hover, branchStart, sourcePort);
            return {};
        }
        result.insert(result.end(), leg.begin() + 1, leg.end());
    }
    return result;
}

POINT nearest_route_point(std::vector<POINT> const& route, POINT wanted) {
    POINT closest = route.front();
    int best = INT_MAX;
    for (POINT point : route) {
        int dx = point.x - wanted.x, dy = point.y - wanted.y;
        int distance = dx * dx + dy * dy;
        if (distance < best) { best = distance; closest = point; }
    }
    return closest;
}

// 结合导线的手动折点、分支起点和端口位置，生成当前需要显示的完整折线路径。
std::vector<POINT> route_wire(Circuit const& circuit, Wire const& wire) {
    auto const* source = find_component(circuit, wire.source);
    auto const* target = find_component(circuit, wire.target);
    if (!source || !target) return {};
    if (wire.parentWire) {
        auto const* parent = find_wire(circuit, wire.parentWire);
        if (!parent || parent->id == wire.id) return {};
        auto parentRoute = route_wire(circuit, *parent);
        if (parentRoute.empty()) return {};
        POINT start = nearest_route_point(parentRoute, wire.tap);
        return route_points(circuit, *source, target, wire.port, wire.bends, nullptr, &start, wire.sourcePort);
    }
    return route_points(circuit, *source, target, wire.port, wire.bends, nullptr, nullptr, wire.sourcePort);
}

// 删除导线时同时处理依附于它的分支，避免留下指向不存在父导线的引用。
void remove_wires(Circuit& circuit, std::set<int> ids) {
    bool changed;
    do {
        changed = false;
        for (auto const& wire : circuit.wires)
            if (wire.parentWire && ids.count(wire.parentWire))
                changed = ids.insert(wire.id).second || changed;
    } while (changed);
    circuit.wires.erase(std::remove_if(circuit.wires.begin(), circuit.wires.end(),
        [&](Wire const& wire) { return ids.count(wire.id) != 0; }), circuit.wires.end());
}

void set_component_direction(Circuit& circuit, int componentId, int direction) {
    Component* component = find_component(circuit, componentId);
    if (!component || component->direction == direction) return;
    component->direction = direction;
    // 折点是旧朝向下的绝对位置；旋转后需要重新规划两端相连的导线。
    std::set<int> reroute;
    for (auto const& wire : circuit.wires)
        if (wire.source == componentId || wire.target == componentId) reroute.insert(wire.id);
    bool changed;
    do {
        changed = false;
        for (auto const& wire : circuit.wires)
            if (wire.parentWire && reroute.count(wire.parentWire))
                changed = reroute.insert(wire.id).second || changed;
    } while (changed);
    for (auto& wire : circuit.wires) if (reroute.count(wire.id)) wire.bends.clear();
}

bool near_polyline(std::vector<POINT> const& points, POINT p, int tolerance = 6) {
    for (size_t i = 1; i < points.size(); ++i) {
        POINT a = points[i - 1], b = points[i];
        if (a.x == b.x && std::abs(p.x - a.x) <= tolerance && p.y >= std::min(a.y, b.y) - tolerance && p.y <= std::max(a.y, b.y) + tolerance) return true;
        if (a.y == b.y && std::abs(p.y - a.y) <= tolerance && p.x >= std::min(a.x, b.x) - tolerance && p.x <= std::max(a.x, b.x) + tolerance) return true;
    }
    return false;
}

bool wire_intersects_rect(std::vector<POINT> const& points, RECT rect) {
    for (size_t i = 1; i < points.size(); ++i) {
        POINT a = points[i - 1], b = points[i];
        if (intersects(RECT{std::min(a.x, b.x), std::min(a.y, b.y),
                            std::max(a.x, b.x), std::max(a.y, b.y)}, rect)) return true;
    }
    return false;
}

using SignalValues = std::map<int, std::array<bool, 2>>;

bool wire_value(SignalValues const& values, Wire const& wire) {
    auto found = values.find(wire.source);
    return found != values.end() && found->second[wire.sourcePort == -2 ? 1 : 0];
}

// 组合逻辑求值：以输入电平和已保存的触发器状态为起点，迭代传播；此函数本身不推进触发器状态。
SignalValues signals(Circuit const& circuit) {
    SignalValues values;
    for (auto const& c : circuit.components) values[c.id] = {(is_source(c.kind) || sequential(c.kind)) && c.value, sequential(c.kind) && !c.value};
    for (size_t pass = 0; pass <= circuit.components.size(); ++pass) {
        bool changed = false;
        for (auto const& c : circuit.components) {
            if (is_source(c.kind) || sequential(c.kind)) continue;
            std::vector<bool> inputs(input_count(c), false);
            for (auto const& wire : circuit.wires)
                if (wire.target == c.id && wire.port >= 0 && wire.port < static_cast<int>(inputs.size()))
                    inputs[wire.port] = wire_value(values, wire);
            bool all = std::all_of(inputs.begin(), inputs.end(), [](bool v) { return v; });
            bool any = std::any_of(inputs.begin(), inputs.end(), [](bool v) { return v; });
            bool parity = std::count(inputs.begin(), inputs.end(), true) % 2 != 0;
            std::array<bool, 2> value{};
            if (c.kind == And) value[0] = all;
            else if (c.kind == Or) value[0] = any;
            else if (c.kind == Not) value[0] = !inputs[0];
            else if (c.kind == Xor) value[0] = parity;
            else if (c.kind == Nand) value[0] = !all;
            else if (c.kind == Nor) value[0] = !any;
            else if (c.kind == Xnor) value[0] = !parity;
            else if (c.kind == HalfAdder) { value[0] = parity; value[1] = inputs[0] && inputs[1]; }
            else if (c.kind == HalfSubtractor) { value[0] = parity; value[1] = !inputs[0] && inputs[1]; }
            else if (c.kind == Equal) value[0] = inputs[0] == inputs[1];
            else value[0] = inputs[0];
            if (values[c.id] != value) { values[c.id] = value; changed = true; }
        }
        if (!changed) break;
    }
    return values;
}

// 推进时序状态：D 在上升沿采样，SET/CLR 高有效且 CLR 优先；RS 为高电平有效、复位优先。迭代次数有上限，并非传播延迟仿真。
void advance_simulation(Circuit& circuit) {
    // 每轮所有状态元件读取同一份已传播快照，保证级联触发器在同一时钟沿采样旧状态。
    for (size_t pass = 0; pass <= circuit.components.size(); ++pass) {
        auto values = signals(circuit);
        bool changed = false;
        for (auto& c : circuit.components) {
            if (!sequential(c.kind)) continue;
            bool in[4]{};
            for (auto const& w : circuit.wires)
                if (w.target == c.id && w.port >= 0 && w.port < 4) in[w.port] = wire_value(values,w);
            bool next = c.value;
            if (c.kind == RSLatch) {
                if (in[1]) next = false; // 明确规定复位优先。
                else if (in[0]) next = true;
            } else {
                if (in[2]) next = false;
                else if (in[3]) next = true; // 异步置位不依赖时钟；CLR 同时有效时仍优先。
                else if (in[1] && !c.lastClock) next = in[0];
                c.lastClock = in[1];
            }
            changed = changed || next != c.value;
            c.value = next;
        }
        if (!changed) break;
    }
}

// 统一的逻辑引脚标注：用于画布和 KiCad 逻辑原理图，不能直接当作实际芯片的物理焊盘编号。
std::string logical_pin_name(Component const& c, int index) {
    if (sequential(c.kind)) {
        if (index < 0) return index == -1 ? "Q" : "~Q";
        if (c.kind == RSLatch) return index == 0 ? "S" : "R";
        return index == 0 ? "D" : index == 1 ? "CLK" : index == 2 ? "CLR" : "SET";
    }
    if (index >= 0) return "A" + std::to_string(index + 1);
    if (c.kind == HalfAdder) return index == -1 ? "S" : "C";
    if (c.kind == HalfSubtractor) return index == -1 ? "D" : "B";
    return "Y";
}

// 本程序所需的轻量 JSON 数据模型；类型转换严格检查，损坏或不匹配的数据通过异常报告。
struct Json {
    enum Type { Object, Array, String, Number, Boolean, Null } type = Null;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    std::string text;
    int number = 0;
    bool boolean = false;
    Json const& get(char const* key) const {
        auto found = object.find(key);
        if (type != Object || found == object.end()) throw std::runtime_error("missing JSON field");
        return found->second;
    }
    int as_int() const {
        if (type != Number) throw std::runtime_error("expected integer");
        return number;
    }
    bool as_bool() const {
        if (type != Boolean) throw std::runtime_error("expected boolean");
        return boolean;
    }
    std::string const& as_string() const {
        if (type != String) throw std::runtime_error("expected string");
        return text;
    }
};

// 解析本程序工程文件中的 JSON；不依赖额外源文件或第三方 JSON 库。
class JsonParser {
    std::string const& text_;
    size_t position_ = 0;
    void space() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\r' ||
               text_[position_] == '\n' || text_[position_] == '\t')) ++position_;
    }
    char next() {
        if (position_ >= text_.size()) throw std::runtime_error("unexpected end of JSON");
        return text_[position_++];
    }
    void expect(char value) {
        space();
        if (next() != value) throw std::runtime_error("invalid JSON punctuation");
    }
    bool consume(char value) {
        space();
        if (position_ < text_.size() && text_[position_] == value) { ++position_; return true; }
        return false;
    }
    std::string string() {
        expect('"');
        std::string result;
        for (;;) {
            char c = next();
            if (c == '"') return result;
            if (c == '\\') {
                c = next();
                if (c == 'n') result.push_back('\n');
                else if (c == 'r') result.push_back('\r');
                else if (c == 't') result.push_back('\t');
                else if (c == '"' || c == '\\' || c == '/') result.push_back(c);
                else throw std::runtime_error("unsupported JSON escape");
            } else result.push_back(c);
        }
    }
    Json value() {
        space();
        if (position_ >= text_.size()) throw std::runtime_error("unexpected end of JSON");
        Json result;
        char c = text_[position_];
        if (c == '{') {
            result.type = Json::Object;
            ++position_;
            if (consume('}')) return result;
            do { std::string key = string(); expect(':'); result.object.emplace(key, value()); }
            while (consume(','));
            expect('}');
        } else if (c == '[') {
            result.type = Json::Array;
            ++position_;
            if (consume(']')) return result;
            do { result.array.push_back(value()); } while (consume(','));
            expect(']');
        } else if (c == '"') {
            result.type = Json::String;
            result.text = string();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            result.type = Json::Number;
            size_t first = position_;
            if (c == '-') ++position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            result.number = std::stoi(text_.substr(first, position_ - first));
        } else if (text_.compare(position_, 4, "true") == 0) {
            result.type = Json::Boolean; result.boolean = true; position_ += 4;
        } else if (text_.compare(position_, 5, "false") == 0) {
            result.type = Json::Boolean; position_ += 5;
        } else if (text_.compare(position_, 4, "null") == 0) {
            position_ += 4;
        } else throw std::runtime_error("invalid JSON value");
        return result;
    }
public:
    explicit JsonParser(std::string const& text) : text_(text) {}
    Json parse() {
        Json result = value();
        space();
        if (position_ != text_.size()) throw std::runtime_error("extra JSON data");
        return result;
    }
};

// 通过 Unicode Windows 文件接口读写，支持中文路径；读取时限制文件大小，避免加载异常大文件。
bool read_file(std::wstring const& path, std::string& result) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart < 10 * 1024 * 1024;
    if (ok) {
        result.resize(static_cast<size_t>(size.QuadPart));
        DWORD count = 0;
        ok = result.empty() || (ReadFile(file, &result[0], static_cast<DWORD>(result.size()), &count, nullptr)
                                && count == result.size());
    }
    CloseHandle(file);
    return ok;
}

bool write_file(std::wstring const& path, std::string const& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) && count == text.size();
    CloseHandle(file);
    return ok;
}

// 原生 JSON 保存：记录元件、连接、折点和分支，同时保存 Q 与时钟历史，避免重新打开后误产生上升沿。
std::string serialize(Circuit const& circuit) {
    std::ostringstream out;
    out << "{\n  \"format\": \"mini-logisim-1\",\n  \"components\": [\n";
    for (size_t i = 0; i < circuit.components.size(); ++i) {
        auto const& c = circuit.components[i];
        out << "    {\"id\": " << c.id << ", \"kind\": \"" << KindKeys[c.kind]
            << "\", \"x\": " << c.x << ", \"y\": " << c.y
            << ", \"direction\": " << c.direction << ", \"inputs\": " << c.inputs
            << ", \"value\": " << (c.value ? "true" : "false")
            << ", \"lastClock\": " << (c.lastClock ? "true" : "false") << "}";
        out << (i + 1 == circuit.components.size() ? "\n" : ",\n");
    }
    out << "  ],\n  \"wires\": [\n";
    for (size_t i = 0; i < circuit.wires.size(); ++i) {
        auto const& w = circuit.wires[i];
        out << "    {\"source\": " << w.source << ", \"target\": " << w.target
            << ", \"port\": \"in" << w.port << "\"";
        if (w.sourcePort == -2) out << ", \"output\": \"out1\"";
        out << ", \"bends\": [";
        for (size_t j = 0; j < w.bends.size(); ++j) {
            out << '[' << w.bends[j].x << ", " << w.bends[j].y << ']';
            if (j + 1 != w.bends.size()) out << ", ";
        }
        out << "]}" << (i + 1 == circuit.wires.size() ? "\n" : ",\n");
    }
    out << "  ]";
    bool hasBranches = std::any_of(circuit.wires.begin(), circuit.wires.end(),
                                   [](Wire const& wire) { return wire.parentWire != 0; });
    if (hasBranches) {
        out << ",\n  \"branches\": [\n";
        bool first = true;
        for (size_t i = 0; i < circuit.wires.size(); ++i) {
            auto const& wire = circuit.wires[i];
            if (!wire.parentWire) continue;
            auto parent = std::find_if(circuit.wires.begin(), circuit.wires.end(),
                [&](Wire const& candidate) { return candidate.id == wire.parentWire; });
            if (parent == circuit.wires.end()) continue;
            if (!first) out << ",\n";
            out << "    {\"wire\": " << i << ", \"parent\": "
                << std::distance(circuit.wires.begin(), parent)
                << ", \"tap\": [" << wire.tap.x << ", " << wire.tap.y << "]}";
            first = false;
        }
        out << "\n  ]";
    }
    out << "\n}\n";
    return out.str();
}

POINT gate_point(POINT center, int direction, int forward, int across) {
    int dx = DirectionX[direction], dy = DirectionY[direction];
    return pt(center.x + forward * dx - across * dy,
              center.y + forward * dy + across * dx);
}

// 导出 .circ 并嵌入本程序的完整 JSON 以支持无损重读；新增时序元件依赖该元数据，不保证第三方 Logisim 能完整还原。
std::string serialize_circ(Circuit const& circuit) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
           "<project source=\"2.7.1\" version=\"1.0\">\n"
           "  <lib desc=\"#Wiring\" name=\"0\"/>\n"
           "  <lib desc=\"#Gates\" name=\"1\"/>\n"
           "  <lib desc=\"#Arithmetic\" name=\"2\"/>\n"
           "  <main name=\"main\"/>\n"
           "  <circuit name=\"main\">\n";
    auto add_wire = [&](POINT a, POINT b) {
        if (same(a, b)) return;
        out << "    <wire from=\"(" << a.x << ',' << a.y << ")\" to=\"("
            << b.x << ',' << b.y << ")\"/>\n";
    };
    auto add_path = [&](POINT a, POINT b, bool horizontalFirst) {
        if (a.x != b.x && a.y != b.y) {
            POINT middle = horizontalFirst ? pt(b.x, a.y) : pt(a.x, b.y);
            add_wire(a, middle);
            a = middle;
        }
        add_wire(a, b);
    };
    static char const* gateNames[] = {"AND Gate", "OR Gate", "NOT Gate", nullptr, nullptr,
        "XOR Gate", "NAND Gate", "NOR Gate", "XNOR Gate"};
    static char const* directionNames[] = {"east", "south", "west", "north"};
    for (auto const& c : circuit.components) {
        if (is_source(c.kind) || c.kind == Output) {
            POINT port = port_point(c, is_source(c.kind) ? -1 : 0);
            out << "    <comp lib=\"0\" loc=\"(" << port.x << ',' << port.y << ")\" name=\"Pin\">\n"
                << "      <a name=\"facing\" val=\"" << directionNames[c.direction] << "\"/>\n"
                << "      <a name=\"output\" val=\"" << (c.kind == Output ? "true" : "false") << "\"/>\n"
                << "    </comp>\n";
        } else if (sequential(c.kind)) {
            out << "    <!-- Sequential component is preserved in mini-logisim-json metadata. -->\n";
        } else if (c.kind <= Xnor) {
            POINT loc = port_point(c, -1);
            out << "    <comp lib=\"1\" loc=\"(" << loc.x << ',' << loc.y
                << ")\" name=\"" << gateNames[c.kind] << "\">\n"
                << "      <a name=\"facing\" val=\"" << directionNames[c.direction] << "\"/>\n";
            if (c.kind != Not)
                out << "      <a name=\"inputs\" val=\"" << input_count(c) << "\"/>\n";
            out << "    </comp>\n";
            if (c.kind == Not) {
                add_path(port_point(c, 0), gate_point(loc, c.direction, -30, 0), false);
            } else {
                int count = input_count(c);
                int axis = 50 + (inverted_gate(c.kind) ? 10 : 0);
                for (int i = 0; i < count; ++i) {
                    int skipStart = count <= 3 ? -10 : -5;
                    int skipDistance = count <= 3 ? 20 : 10;
                    int lowerEven = count <= 3 ? 20 : 10;
                    int across = count % 2 ? skipStart * (count - 1) + skipDistance * i :
                                 skipStart * count + skipDistance * i + (i >= count / 2 ? lowerEven : 0);
                    add_path(port_point(c, i), gate_point(loc, c.direction, -axis, across), false);
                }
            }
        } else {
            POINT loc = port_point(c, -1);
            char const* name = c.kind == HalfAdder ? "Adder" : c.kind == HalfSubtractor ? "Subtractor" : "Comparator";
            out << "    <comp lib=\"2\" loc=\"(" << loc.x << ',' << loc.y
                << ")\" name=\"" << name << "\">\n"
                << "      <a name=\"width\" val=\"1\"/>\n"
                << "    </comp>\n";
            add_path(port_point(c, 0), pt(loc.x - 40, loc.y - 10), false);
            add_path(port_point(c, 1), pt(loc.x - 40, loc.y + 10), false);
            if (c.kind != Equal) {
                POINT carryIn = pt(loc.x - 20, loc.y - 20);
                POINT zero = pt(loc.x - 60, loc.y - 40);
                out << "    <comp lib=\"0\" loc=\"(" << zero.x << ',' << zero.y
                    << ")\" name=\"Constant\">\n"
                    << "      <a name=\"value\" val=\"0x0\"/>\n"
                    << "    </comp>\n";
                add_path(zero, carryIn, true);
                add_path(pt(loc.x - 20, loc.y + 20), port_point(c, -2), true);
            }
        }
    }
    for (auto const& wire : circuit.wires) {
        auto route = route_wire(circuit, wire);
        for (size_t i = 1; i < route.size(); ++i) add_wire(route[i - 1], route[i]);
    }
    out << "  </circuit>\n";
    std::string saved = serialize(circuit);
    static constexpr char digits[] = "0123456789ABCDEF";
    out << "  <!-- mini-logisim-json:";
    for (unsigned char byte : saved) out << digits[byte >> 4] << digits[byte & 15];
    out << " -->\n</project>\n";
    return out.str();
}

// KiCad 导出辅助函数；所有实现保留在当前文件，便于单独复制编译。
std::string kicad_quote(std::string const& text) {
    std::string result = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result + '"';
}

std::string kicad_uuid() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("cannot create UUID");
    wchar_t buffer[40]{};
    StringFromGUID2(id, buffer, 40);
    std::string result;
    for (int i = 1; i < 37; ++i) result += static_cast<char>(buffer[i]);
    return result;
}

// 实际硬件合成：使用 5V 74HCT 系列，应用实现仍保持单文件。
// 实际硬件元件：ref 为 PCB 位号，footprint 为 KiCad 库封装，pins 将物理焊盘编号映射到网络名称。
struct PcbPart {
    std::string ref, value, footprint;
    std::map<int, std::string> pins;
};
// 硬件合成辅助模型：分配芯片中的门、生成中间网络，并为芯片添加供电和去耦。
struct PcbCircuit {
    std::vector<PcbPart> parts;
    std::map<std::string, int> counters;
    int nandChip = -1, nandSlot = 4, invChip = -1, invSlot = 6, nextNet = 0;
    int add(std::string prefix, std::string value, std::string footprint,
            std::map<int, std::string> pins) {
        std::string ref = prefix + std::to_string(++counters[prefix]);
        parts.push_back({ref, value, footprint, pins});
        return static_cast<int>(parts.size()) - 1;
    }
    // 芯片采用 SOIC-14，7 脚接地、14 脚供电；每颗芯片配一只 100 nF 去耦电容。
    int chip(std::string value) {
        int index = add("U", value, "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm", {{7,"GND"},{14,"+5V"}});
        add("C", "100n", "Capacitor_SMD:C_0805_2012Metric", {{1,"+5V"},{2,"GND"}});
        return index;
    }
    // 按 74HCT00 的四组物理引脚顺序分配与非门；forced 用于 RS 反馈输出的预先命名。
    std::string nand_gate(std::string a, std::string b, std::string forced = "") {
        static int const pins[4][3] = {{1,2,3},{4,5,6},{9,10,8},{12,13,11}};
        if (nandSlot == 4) { nandChip = chip("74HCT00D"); nandSlot = 0; }
        auto const& p = pins[nandSlot++];
        std::string net = forced.empty() ? "N" + std::to_string(++nextNet) : forced;
        parts[nandChip].pins[p[0]] = a; parts[nandChip].pins[p[1]] = b;
        parts[nandChip].pins[p[2]] = net;
        return net;
    }
    std::string invert(std::string a) { return nand_gate(a,a); }
    std::string and_gate(std::string a, std::string b) { return invert(nand_gate(a,b)); }
    std::string or_gate(std::string a, std::string b) {
        auto x = invert(a), y = invert(b); return nand_gate(x,y);
    }
    // 用四个与非门实现异或；目前不跨逻辑块消除重复的中间运算。
    std::string xor_gate(std::string a, std::string b) {
        auto t = nand_gate(a,b), x = nand_gate(a,t), y = nand_gate(b,t);
        return nand_gate(x,y);
    }
    // 将非门分配到 74HCT14 的六个施密特反相器中，保持逻辑反相功能及真实引脚映射。
    std::string schmitt(std::string a) {
        static int const pins[6][2] = {{1,2},{3,4},{5,6},{9,8},{11,10},{13,12}};
        if (invSlot == 6) { invChip = chip("74HCT14D"); invSlot = 0; }
        auto const& p = pins[invSlot++];
        std::string net = "N" + std::to_string(++nextNet);
        parts[invChip].pins[p[0]] = a; parts[invChip].pins[p[1]] = net;
        return net;
    }
    // 收尾处理 74HCT00/14 的未用门：输入接地，输出各用独立未连接网络；74HCT74 的未用半片在分配时处理。
    void finish() {
        for (auto& p : parts) {
            if (p.value != "74HCT00D" && p.value != "74HCT14D") continue;
            std::set<int> inputs = p.value == "74HCT00D" ?
                std::set<int>{1,2,4,5,9,10,12,13} : std::set<int>{1,3,5,9,11,13};
            for (int pin = 1; pin <= 14; ++pin) if (!p.pins.count(pin))
                p.pins[pin] = inputs.count(pin) ? "GND" :
                    "unconnected-(" + p.ref + "-Pad" + std::to_string(pin) + ")";
        }
    }
};
// 把画布逻辑转换为真实硬件：检查连接、分配信号接口和状态输出，再按依赖生成门电路。允许状态反馈，拒绝无法解析的组合环路。
PcbCircuit build_pcb_circuit(Circuit const& circuit) {
    using Key = std::pair<int,int>;
    std::map<int, Component const*> components;
    std::set<Key> pins;
    std::map<Key, Key> connections;
    for (auto const& c : circuit.components) {
        if (c.id <= 0 || c.kind < And || c.kind > Constant || c.direction < 0 || c.direction > 3 ||
            input_count(c) < 1 || input_count(c) > 8 || !components.emplace(c.id,&c).second)
            throw std::runtime_error("Invalid or duplicate component.");
        for (auto const& p : ports(c)) pins.insert({c.id,p.index});
    }
    for (auto const& w : circuit.wires) {
        Key source{w.source,w.sourcePort}, target{w.target,w.port};
        if (!pins.count(source) || !pins.count(target) || w.sourcePort >= 0 || w.port < 0)
            throw std::runtime_error("Invalid wire endpoint.");
        if (connections.count(target) && connections.at(target) != source)
            throw std::runtime_error("Multiple outputs drive one input.");
        connections[target] = source;
    }
    PcbCircuit pcb;
    if (components.empty()) return pcb;
    std::string connector = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical";
    pcb.add("J","5V_IN",connector,{{1,"+5V"},{2,"GND"}});
    std::map<Key,std::string> outputs;
    std::set<int> pending;
    std::map<int,int> flipChips;
    for (auto const& entry : components) {
        auto const& c = *entry.second;
        if (c.kind == Constant) {
            outputs[{c.id,-1}] = c.value ? "+5V" : "GND";
        } else if (is_source(c.kind)) {
            std::string net = (c.kind == Clock ? "CLK" : "IN") + std::to_string(c.id);
            pcb.add("J",net,connector,{{1,net},{2,"GND"}});
            pcb.add("R","10k","Resistor_SMD:R_0805_2012Metric",{{1,net},{2,"GND"}});
            outputs[{c.id,-1}] = net;
        } else {
            pending.insert(c.id);
            if (sequential(c.kind)) {
                outputs[{c.id,-1}] = "Q" + std::to_string(c.id);
                outputs[{c.id,-2}] = "QB" + std::to_string(c.id);
                if (c.kind == DFlipFlop) {
                    int index = pcb.chip("74HCT74D");
                    flipChips[c.id] = index;
                    // 使用第一组 D 触发器；第二组固定复位，并为其全部输入指定电平。
                    pcb.parts[index].pins.insert({{4,"+5V"},{5,outputs[{c.id,-1}]},{6,outputs[{c.id,-2}]},
                        {10,"+5V"},{11,"GND"},{12,"GND"},{13,"GND"}});
                    pcb.parts[index].pins[8] = "unconnected-(" + pcb.parts[index].ref + "-Pad8)";
                    pcb.parts[index].pins[9] = "unconnected-(" + pcb.parts[index].ref + "-Pad9)";
                }
            }
        }
    }
    while (!pending.empty()) {
        bool progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            auto const& c = *components.at(*it);
            std::vector<std::string> ins;
            for (int i = 0; i < input_count(c); ++i) {
                auto link = connections.find({c.id,i});
                if (link == connections.end()) ins.push_back("GND");
                else if (outputs.count(link->second)) ins.push_back(outputs.at(link->second));
                else break;
            }
            if (ins.size() != static_cast<size_t>(input_count(c))) { ++it; continue; }
            std::string result = ins[0];
            if (c.kind == Output) pcb.add("J","OUT"+std::to_string(c.id),connector,{{1,result},{2,"GND"}});
            else if (c.kind == Not) result = pcb.schmitt(result);
            else if (c.kind == DFlipFlop) {
                auto clear = pcb.invert(ins[2]);
                // 74HCT74 的异步端低有效；CLR 抑制 SET，避免同时拉低 PRE/CLR。
                auto preset = pcb.nand_gate(ins[3],clear);
                auto& p = pcb.parts[flipChips.at(c.id)];
                p.pins[1] = clear; p.pins[2] = ins[0]; p.pins[3] = ins[1]; p.pins[4] = preset;
                result = outputs.at({c.id,-1});
            } else if (c.kind == RSLatch) {
                auto nr = pcb.invert(ins[1]);
                auto ns = pcb.nand_gate(ins[0],nr); // R 为高时抑制 S，使硬件与仿真的复位优先规则一致。
                result = outputs.at({c.id,-1});
                auto qb = outputs.at({c.id,-2});
                pcb.nand_gate(ns,qb,result);
                pcb.nand_gate(nr,result,qb);
            }
            else {
                for (size_t i = 1; i < ins.size(); ++i) {
                    if (c.kind == And || c.kind == Nand) result = pcb.and_gate(result,ins[i]);
                    else if (c.kind == Or || c.kind == Nor) result = pcb.or_gate(result,ins[i]);
                    else result = pcb.xor_gate(result,ins[i]);
                }
                if (c.kind == Nand || c.kind == Nor || c.kind == Xnor || c.kind == Equal) result = pcb.invert(result);
                if (c.kind == HalfAdder) outputs[{c.id,-2}] = pcb.and_gate(ins[0],ins[1]);
                if (c.kind == HalfSubtractor) outputs[{c.id,-2}] = pcb.and_gate(pcb.invert(ins[0]),ins[1]);
            }
            outputs[{c.id,-1}] = result;
            it = pending.erase(it); progress = true;
        }
        if (!progress) throw std::runtime_error("Feedback circuits cannot be synthesized into combinational hardware.");
    }
    pcb.finish();
    return pcb;
}
// 输出 KiCad version E 硬件网表：包含真实器件、标准库封装、物理引脚及连接，不是抽象逻辑符号清单。
std::string serialize_kicad_net(Circuit const& circuit) {
    auto pcb = build_pcb_circuit(circuit);
    auto pin_info = [](PcbPart const& p, int n) -> std::pair<std::string,std::string> {
        if (p.value == "74HCT74D") {
            static char const* names[] = {"","~CLR1","D1","CLK1","~PRE1","Q1","~Q1","GND","~Q2","Q2","~PRE2","CLK2","D2","~CLR2","VCC"};
            return {names[n], n == 7 || n == 14 ? "power_in" : n == 5 || n == 6 || n == 8 || n == 9 ? "output" : "input"};
        }
        if (p.value == "74HCT00D" || p.value == "74HCT14D") {
            if (n == 7) return {"GND","power_in"};
            if (n == 14) return {"VCC","power_in"};
            bool output = p.value == "74HCT00D" ? (n==3 || n==6 || n==8 || n==11) :
                (n==2 || n==4 || n==6 || n==8 || n==10 || n==12);
            return {std::string(output ? "Y" : "A")+std::to_string(n), output ? "output" : "input"};
        }
        return {std::to_string(n),"passive"};
    };
    auto part_name = [](PcbPart const& p) {
        return p.ref[0] == 'U' ? p.value : p.ref[0] == 'J' ? std::string("Conn_01x02") :
            p.ref[0] == 'R' ? std::string("R") : std::string("C");
    };
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "(export\n  (version \"E\")\n  (design (source \"MiniLogisim hardware\") (tool \"MiniLogisim\"))\n  (components\n";
    std::map<std::string,std::vector<std::pair<size_t,int>>> nets;
    for (size_t i = 0; i < pcb.parts.size(); ++i) {
        auto const& p = pcb.parts[i];
        uint64_t stamp = 14695981039346656037ULL;
        for (unsigned char c : p.ref) { stamp ^= c; stamp *= 1099511628211ULL; }
        out << "    (comp\n      (ref " << kicad_quote(p.ref) << ")\n      (value " << kicad_quote(p.value)
            << ")\n      (footprint " << kicad_quote(p.footprint) << ")\n"
            << "      (libsource (lib \"MiniLogisimHardware\") (part " << kicad_quote(part_name(p)) << "))\n"
            << "      (sheetpath (names \"/\") (tstamps \"/\"))\n"
            << "      (tstamps \"00000000-0000-4000-8000-" << std::hex << std::setw(12) << std::setfill('0')
            << (stamp & 0xffffffffffffULL) << std::dec << "\"))\n";
        for (auto const& pin : p.pins) nets[pin.second].push_back({i,pin.first});
    }
    out << "  )\n  (groups)\n  (variants)\n  (libparts\n";
    std::set<std::string> definitions;
    for (auto const& p : pcb.parts) {
        if (!definitions.insert(part_name(p)).second) continue;
        out << "    (libpart (lib \"MiniLogisimHardware\") (part " << kicad_quote(part_name(p)) << ")\n      (pins\n";
        for (auto const& pin : p.pins) {
            auto info = pin_info(p,pin.first);
            out << "        (pin (num \"" << pin.first << "\") (name " << kicad_quote(info.first)
                << ") (type " << kicad_quote(info.second) << "))\n";
        }
        out << "      ))\n";
    }
    out << "  )\n  (libraries)\n  (nets\n";
    int code = 0;
    for (auto const& net : nets) {
        out << "    (net (code \"" << ++code << "\") (name " << kicad_quote(net.first) << ") (class \"Default\")\n";
        for (auto const& node : net.second) {
            auto const& p = pcb.parts[node.first];
            auto info = pin_info(p,node.second);
            out << "      (node (ref " << kicad_quote(p.ref) << ") (pin \"" << node.second
                << "\") (pinfunction " << kicad_quote(info.first) << ") (pintype " << kicad_quote(info.second) << "))\n";
        }
        out << "    )\n";
    }
    out << "  )\n)\n";
    return out.str();
}


// 输出可编辑的 KiCad 逻辑原理图，内嵌符号定义；它不等同于硬件网表，不能直接用于更新已导入硬件的 PCB。
std::string serialize_kicad(Circuit const& circuit, std::string const& project) {
    constexpr double scale = 0.127; // 画布上一个 20 像素网格对应 100 mil，即 2.54 mm。
    using Net = std::pair<int, int>;
    using XY = std::pair<int, int>;
    struct Segment { POINT a, b; Net net; };
    struct Pin { POINT point; Net net; };
    std::map<Net, Net> connections;
    for (auto const& w : circuit.wires) connections[{w.target, w.port}] = {w.source, w.sourcePort};
    std::vector<Pin> pins;
    std::map<XY, Net> occupied;
    for (auto const& c : circuit.components) {
        for (auto const& p : ports(c)) {
            Net key{c.id, p.index};
            Net net = connections.count(key) ? connections.at(key) : key;
            XY point{p.point.x, p.point.y};
            if (occupied.count(point) && occupied.at(point) != net)
                throw std::runtime_error("Different signal pins overlap. Move the components apart before exporting.");
            occupied[point] = net;
            pins.push_back({p.point, net});
        }
    }
    std::vector<Segment> segments;
    std::set<Net> labeled;
    for (auto const& w : circuit.wires) {
        Net net{w.source, w.sourcePort};
        auto route = route_wire(circuit, w);
        if (route.size() < 2) { labeled.insert(net); continue; }
        // 去掉寻路产生的中间网格顶点，避免无关交叉点被 KiCad 识别为电气连接。
        POINT start = route.front(), previous = start;
        for (size_t i = 1; i < route.size(); ++i) {
            POINT next = route[i];
            if (same(previous, next)) continue;
            if (!same(start, previous) && !((start.x == previous.x && previous.x == next.x) ||
                                           (start.y == previous.y && previous.y == next.y))) {
                segments.push_back({start, previous, net});
                start = previous;
            }
            previous = next;
        }
        if (!same(start, previous)) segments.push_back({start, previous, net});
    }
    auto on_segment = [](POINT p, Segment const& s) {
        return ((s.a.x == s.b.x && p.x == s.a.x) || (s.a.y == s.b.y && p.y == s.a.y)) &&
            p.x >= std::min(s.a.x, s.b.x) && p.x <= std::max(s.a.x, s.b.x) &&
            p.y >= std::min(s.a.y, s.b.y) && p.y <= std::max(s.a.y, s.b.y);
    };
    // KiCad 会连接相触端点和重叠导线；对存在冲突的路径改用同名网络标签，
    // 以保留原电路的连接关系，避免因几何位置产生误短接。
    for (size_t i = 0; i < segments.size(); ++i) {
        auto const& a = segments[i];
        for (auto const& pin : pins)
            if (a.net != pin.net && on_segment(pin.point, a)) labeled.insert(a.net);
        for (size_t j = i + 1; j < segments.size(); ++j) {
            auto const& b = segments[j];
            if (a.net != b.net && (on_segment(a.a, b) || on_segment(a.b, b) ||
                                  on_segment(b.a, a) || on_segment(b.b, a))) {
                labeled.insert(a.net); labeled.insert(b.net);
            }
        }
    }
    std::map<Net, std::set<XY>> nodes;
    for (auto const& pin : pins) nodes[pin.net].insert({pin.point.x, pin.point.y});
    for (auto const& s : segments) {
        nodes[s.net].insert({s.a.x, s.a.y}); nodes[s.net].insert({s.b.x, s.b.y});
    }
    for (auto const& a : segments) for (auto const& b : segments) {
        if (a.net == b.net && a.a.x == a.b.x && b.a.y == b.b.y) {
            POINT cross = pt(a.a.x, b.a.y);
            if (on_segment(cross, a) && on_segment(cross, b)) nodes[a.net].insert({cross.x, cross.y});
        }
    }
    // 即使结点属于同一网络，放在另一网络的交叉线上也会误短接，因此还需检查交叉位置。
    for (auto const& s : segments) for (auto const& group : nodes) {
        if (s.net == group.first) continue;
        for (auto const& p : group.second)
            if (on_segment(pt(p.first, p.second), s)) labeled.insert(s.net);
    }

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (auto const& c : circuit.components) {
        int extent = std::max(80, half_height(c) + 40);
        minX = std::min(minX, c.x - extent); minY = std::min(minY, c.y - extent);
        maxX = std::max(maxX, c.x + extent); maxY = std::max(maxY, c.y + extent);
    }
    for (auto const& s : segments) for (POINT p : {s.a, s.b}) {
        minX = std::min(minX, static_cast<int>(p.x)); minY = std::min(minY, static_cast<int>(p.y));
        maxX = std::max(maxX, static_cast<int>(p.x)); maxY = std::max(maxY, static_cast<int>(p.y));
    }
    double offsetX = 25.4 - std::floor(minX / 20.0) * 2.54;
    double offsetY = 25.4 - std::floor(minY / 20.0) * 2.54;
    double width = std::max(297.0, maxX * scale + offsetX + 25.4);
    double height = std::max(210.0, maxY * scale + offsetY + 25.4);
    if (width > 1200 || height > 1200) throw std::runtime_error("Circuit exceeds KiCad's 1200 mm page size. Move components closer together.");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4);
    std::string root = kicad_uuid();
    out << "(kicad_sch (version 20250114) (generator \"mini_logisim\")\n"
        << "  (uuid " << root << ") (paper \"User\" " << width << ' ' << height << ")\n  (lib_symbols\n";
    auto name = [](Component const& c) { return std::string(KindKeys[c.kind]) + "_" + std::to_string(input_count(c)); };
    static char const* captions[] = {"&", ">=1", "1", "IN", "OUT", "=1", "&", ">=1", "=1", "HA", "HS", "=", "D FF", "RS", "CLK", "CONST"};
    auto pin_name = logical_pin_name;
    std::set<std::string> definitions;
    for (auto c : circuit.components) {
        std::string symbol = name(c);
        if (!definitions.insert(symbol).second) continue;
        c.x = c.y = c.direction = 0;
        double h = (is_source(c.kind) || c.kind == Output ? 14 : half_height(c)) * scale;
        out << "    (symbol \"MiniLogisim:" << symbol << "\" (pin_names (offset 0.508))"
            << " (in_bom yes) (on_board yes)\n"
            << "      (property \"Reference\" \"U\" (at 0 " << h + 2.54 << " 0) (effects (font (size 1.27 1.27))))\n"
            << "      (property \"Value\" \"" << symbol << "\" (at 0 " << -h - 2.54 << " 0) (effects (font (size 1.27 1.27))))\n"
            << "      (symbol \"" << symbol << "_0_1\"\n"
            << "        (rectangle (start " << (sequential(c.kind) ? -7.62 : -2.54) << " " << h << ") (end " << (sequential(c.kind) ? 7.62 : 2.54) << " " << -h
            << ") (stroke (width 0.254) (type default)) (fill (type background)))\n"
            << "        (text \"" << captions[c.kind] << "\" (at 0 0 0) (effects (font (size 1.016 1.016))))\n      )\n"
            << "      (symbol \"" << symbol << "_1_1\"\n";
        auto localPins = ports(c);
        for (size_t i = 0; i < localPins.size(); ++i) {
            auto const& p = localPins[i];
            bool output = p.index < 0;
            out << "        (pin " << (output ? "output" : "input") << ' '
                << (c.kind == DFlipFlop && p.index == 1 ? "clock" : output && inverted_gate(c.kind) ? "inverted" : "line")
                << " (at " << p.point.x * scale << ' ' << -p.point.y * scale << ' ' << (c.kind == DFlipFlop && p.index == 2 ? 90 : c.kind == DFlipFlop && p.index == 3 ? 270 : output ? 180 : 0)
                << ") (length 2.54) (name \"" << pin_name(c, p.index) << "\" (effects (font (size 0.762 0.762))))"
                << " (number \"" << i + 1 << "\" (effects (font (size 0.762 0.762)))))\n";
        }
        out << "      )\n    )\n";
    }
    out << "  )\n";
    std::set<std::tuple<int, int, int, int>> edges;
    std::map<XY, int> degrees;
    for (auto const& s : segments) {
        if (labeled.count(s.net)) continue;
        std::vector<XY> cuts;
        for (auto const& p : nodes[s.net]) if (on_segment(pt(p.first, p.second), s)) cuts.push_back(p);
        for (size_t i = 1; i < cuts.size(); ++i) {
            auto a = cuts[i - 1], b = cuts[i];
            if (!edges.insert({a.first, a.second, b.first, b.second}).second) continue;
            ++degrees[a]; ++degrees[b];
            out << "  (wire (pts (xy " << a.first * scale + offsetX << ' ' << a.second * scale + offsetY
                << ") (xy " << b.first * scale + offsetX << ' ' << b.second * scale + offsetY
                << ")) (stroke (width 0) (type default)) (uuid " << kicad_uuid() << "))\n";
        }
    }
    std::set<XY> pinPositions;
    for (auto const& p : pins) if (!labeled.count(p.net)) pinPositions.insert({p.point.x, p.point.y});
    for (auto const& p : pinPositions) ++degrees[p];
    for (auto const& node : degrees) if (node.second >= 3) {
        out << "  (junction (at " << node.first.first * scale + offsetX << ' ' << node.first.second * scale + offsetY
            << ") (diameter 0) (color 0 0 0 0) (uuid " << kicad_uuid() << "))\n";
    }
    std::set<XY> labels;
    for (auto const& pin : pins) if (labeled.count(pin.net) && labels.insert({pin.point.x, pin.point.y}).second) {
        out << "  (label \"ML_" << pin.net.first << "_" << -pin.net.second
            << "\" (at " << pin.point.x * scale + offsetX << ' ' << pin.point.y * scale + offsetY
            << " 0) (effects (font (size 1.016 1.016)) (justify left bottom)) (uuid " << kicad_uuid() << "))\n";
    }
    for (auto const& c : circuit.components) {
        double x = c.x * scale + offsetX, y = c.y * scale + offsetY;
        double h = (c.direction % 2 ? 40 : half_height(c)) * scale + 2.54;
        std::string reference = "U" + std::to_string(c.id);
        out << "  (symbol (lib_id \"MiniLogisim:" << name(c) << "\") (at " << x << ' ' << y << ' '
            << (360 - c.direction * 90) % 360 << ") (unit 1) (in_bom yes) (on_board yes) (dnp no)\n"
            << "    (uuid " << kicad_uuid() << ")\n"
            << "    (property \"Reference\" \"" << reference << "\" (at " << x << ' ' << y - h
            << " 0) (effects (font (size 1.27 1.27))))\n"
            << "    (property \"Value\" \"" << name(c) << "\" (at " << x << ' ' << y + h
            << " 0) (effects (font (size 1.27 1.27))))\n";
        if (is_source(c.kind)) out << "    (property \"LogisimInputValue\" \"" << (c.value ? "1" : "0")
            << "\" (at " << x << ' ' << y << " 0) (effects (font (size 1.27 1.27)) (hide yes)))\n";
        for (size_t i = 0; i < ports(c).size(); ++i)
            out << "    (pin \"" << i + 1 << "\" (uuid " << kicad_uuid() << "))\n";
        out << "    (instances (project " << kicad_quote(project) << " (path \"/" << root
            << "\" (reference \"" << reference << "\") (unit 1))))\n  )\n";
    }
    out << "  (sheet_instances (path \"/\" (page \"1\")))\n)\n";
    return out.str();
}

// 加载原生工程并校验元件和连接；兼容旧文件缺少 lastClock 的情况，保留新文件的时钟历史。
Circuit deserialize(std::string const& text) {
    Json root = JsonParser(text).parse();
    if (root.get("format").as_string() != "mini-logisim-1") throw std::runtime_error("wrong circuit format");
    Circuit result;
    Json const& components = root.get("components"), &wires = root.get("wires");
    if (components.type != Json::Array || wires.type != Json::Array) throw std::runtime_error("invalid arrays");
    std::set<int> ids;
    for (auto const& item : components.array) {
        Component c;
        c.id = item.get("id").as_int();
        std::string kind = item.get("kind").as_string();
        int index = 0;
        constexpr int kindCount = static_cast<int>(sizeof(KindKeys) / sizeof(KindKeys[0]));
        for (; index < kindCount && kind != KindKeys[index]; ++index) {}
        if (index == kindCount) throw std::runtime_error("unknown component");
        c.kind = static_cast<Kind>(index);
        c.x = item.get("x").as_int(); c.y = item.get("y").as_int();
        c.direction = item.get("direction").as_int();
        c.inputs = item.get("inputs").as_int();
        c.value = item.get("value").as_bool();
        if (item.object.count("lastClock")) c.lastClock = item.get("lastClock").as_bool();
        if (c.id < 1 || c.direction < 0 || c.direction > 3 || c.inputs < 1 || c.inputs > 8 || !ids.insert(c.id).second)
            throw std::runtime_error("invalid component");
        result.nextComponent = std::max(result.nextComponent, c.id + 1);
        result.components.push_back(c);
    }
    for (auto const& item : wires.array) {
        Wire w;
        w.id = result.nextWire++;
        w.source = item.get("source").as_int();
        w.target = item.get("target").as_int();
        std::string port = item.get("port").as_string();
        if (port.size() != 3 || port.substr(0, 2) != "in" || port[2] < '0' || port[2] > '7')
            throw std::runtime_error("invalid wire port");
        w.port = port[2] - '0';
        auto output = item.object.find("output");
        if (output != item.object.end()) {
            if (output->second.as_string() != "out1") throw std::runtime_error("invalid output port");
            w.sourcePort = -2;
        }
        auto const* source = find_component(result, w.source);
        auto const* target = find_component(result, w.target);
        if (!source || !target || (w.source == w.target && !sequential(source->kind)) || source->kind == Output || is_source(target->kind) ||
            w.port >= input_count(*target) || -w.sourcePort > output_count(*source))
            throw std::runtime_error("invalid wire connection");
        Json const& bends = item.get("bends");
        if (bends.type != Json::Array) throw std::runtime_error("invalid bends");
        for (auto const& bend : bends.array) {
            if (bend.type != Json::Array || bend.array.size() != 2) throw std::runtime_error("invalid bend");
            w.bends.push_back(pt(bend.array[0].as_int(), bend.array[1].as_int()));
        }
        std::set<int> replaced;
        for (auto const& prior : result.wires)
            if (prior.target == w.target && prior.port == w.port) replaced.insert(prior.id);
        remove_wires(result, replaced);
        result.wires.push_back(w);
    }
    auto branches = root.object.find("branches");
    if (branches != root.object.end()) {
        if (branches->second.type != Json::Array) throw std::runtime_error("invalid branches");
        for (auto const& item : branches->second.array) {
            int index = item.get("wire").as_int();
            int parentIndex = item.get("parent").as_int();
            Json const& tap = item.get("tap");
            if (index < 0 || parentIndex < 0 || parentIndex >= index ||
                index >= static_cast<int>(wires.array.size()) ||
                tap.type != Json::Array || tap.array.size() != 2)
                throw std::runtime_error("invalid branch");
            Wire* wire = find_wire(result, index + 1);
            Wire const* parent = find_wire(result, parentIndex + 1);
            if (!wire || !parent || wire->source != parent->source || wire->parentWire)
                throw std::runtime_error("invalid branch parent");
            POINT point = pt(tap.array[0].as_int(), tap.array[1].as_int());
            if (point.x % Grid || point.y % Grid) throw std::runtime_error("invalid branch tap");
            wire->parentWire = parent->id;
            wire->tap = point;
        }
    }
    return result;
}

// 从本程序写入的 .circ 元数据中恢复完整工程；这里不是通用 Logisim 文件导入器。
Circuit deserialize_circ(std::string const& text) {
    std::string const marker = "<!-- mini-logisim-json:";
    size_t begin = text.find(marker);
    if (begin == std::string::npos) throw std::runtime_error("unsupported Logisim project");
    begin += marker.size();
    size_t end = text.find(" -->", begin);
    if (end == std::string::npos || (end - begin) % 2) throw std::runtime_error("invalid project data");
    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("invalid project data");
    };
    std::string json;
    for (size_t i = begin; i < end; i += 2)
        json.push_back(static_cast<char>((hex_value(text[i]) << 4) | hex_value(text[i + 1])));
    return deserialize(json);
}

enum CommandId {
    FileNew = 100, FileOpen, FileSave, FileSaveAs, FileExit,
    EditSelectAll, EditDelete, EditRotate,
    SimToggle, SimReset, HelpAbout, CountMinus, CountPlus,
    ZoomIn, ZoomOut, ZoomReset, EditUndo,
    ToolBase = 200, CountEdit = 220, DirectionCombo = 221, RotateButton = 222, LibraryTree = 223
};

// 波形每 100 ms 连续采样稳定电平；横轴为采样时间，不模拟芯片传播延迟。
// 拓扑改变后重新开始，移动/缩放不额外采样；最多保留最近 256 个采样点，记录不写入工程文件。
struct WaveHistory {
    std::string topology;
    std::vector<std::wstring> labels;
    std::vector<std::vector<bool>> samples;
    size_t firstStep=0;
    void capture(Circuit const& circuit, bool force=false, bool recordChanges=true) {
        std::ostringstream key;
        for(auto const& c:circuit.components) key<<c.id<<':'<<c.kind<<':'<<input_count(c)<<';';
        for(auto const& w:circuit.wires) key<<w.source<<':'<<w.sourcePort<<'>'<<w.target<<':'<<w.port<<';';
        if(topology!=key.str()){topology=key.str();samples.clear();firstStep=0;}
        labels.clear();std::vector<bool> sample;auto values=signals(circuit);
        for(auto const& c:circuit.components) for(auto const& port:ports(c)) {
            std::wstring pin;
            if(c.kind==DFlipFlop) pin=port.index==-1?L"Q":port.index==-2?L"~Q":port.index==0?L"D":port.index==1?L"CLK":port.index==2?L"CLR":L"SET";
            else if(c.kind==RSLatch) pin=port.index==-1?L"Q":port.index==-2?L"~Q":port.index==0?L"S":L"R";
            else pin=port.index<0?L"OUT"+std::to_wstring(-port.index):L"IN"+std::to_wstring(port.index+1);
            labels.push_back(L"U"+std::to_wstring(c.id)+L"."+pin);
            bool value=false;
            if(port.index<0)value=values.at(c.id)[port.index==-2?1:0];
            else for(auto const& w:circuit.wires)if(w.target==c.id && w.port==port.index)value=wire_value(values,w);
            sample.push_back(value);
        }
        if(!sample.empty() && (force || samples.empty() || (recordChanges && sample!=samples.back())))samples.push_back(sample);
        if(samples.size()>256){samples.erase(samples.begin());++firstStep;}
    }
};

// 应用控制器：管理窗口、工具、选择状态、撤销快照、文件操作及画布交互。
class App {
public:
    struct UndoState {
        Circuit circuit;
        std::set<int> selectedComponents, selectedWires;
    };
    HWND mainWindow = nullptr, canvas = nullptr, selectionLabel = nullptr;
    HWND libraryTree = nullptr;


    HWND countEdit = nullptr, countMinus = nullptr, countPlus = nullptr;
    HWND directionCombo = nullptr, rotateButton = nullptr;
    wxBitmapToggleButton* toolButtons[7]{};
    HFONT uiFont = nullptr, gateFont = nullptr;

    Circuit circuit;
    std::vector<UndoState> undoHistory;
    std::wstring path;

    Tool tool = SelectTool;
    std::set<int> selectedComponents, selectedWires;
    bool updatingControls = false;
    bool draggingComponent = false, dragged = false, marquee = false, dragUndoRecorded = false;
    POINT press{}, dragOffset{}, dragStartPosition{}, marqueeStart{}, marqueeEnd{}, hover{};
    bool hoverValid = false;

    int dragComponentId = 0, wireStartId = 0, wireStartPort = -1, branchFromWireId = 0;
    int zoomPercent = 100, viewX = 0, viewY = 0, wheelRemainder = 0;
    POINT branchPoint{};
    std::vector<POINT> bends;

    // wxWidgets 管理窗口、控件和事件；HWND 仅供现有 Windows 画布/文件接口使用。
    wxFrame* frame = nullptr;
    wxPanel* drawing = nullptr;
    wxTreeCtrl* tree = nullptr;
    wxStaticText* selection = nullptr;
    wxTextCtrl* countBox = nullptr;
    wxChoice* directions = nullptr;
    wxTextCtrl* helpText = nullptr;
    wxPopupWindow* hint = nullptr;
    wxStaticText* hintLabel = nullptr;
    wxFont menuFont;
    wxScrolledWindow* waveform = nullptr;
    WaveHistory waves;
    wxTimer* waveTimer = nullptr;
    void update_waveform(bool force=false);
    void paint_waveform();
    Tool libraryDragTool = SelectTool;
    std::map<int, wxTreeItemId> libraryItems;
    void create_ui();
    void show_hint(wxString const& text, wxRect anchor = wxRect());

    POINT screen_to_world(POINT point) const;
    void zoom_at(int direction, POINT anchor);
    void zoom_center(int direction);
    bool wheel_zoom(POINT screen, int delta);
    void refresh() { advance_simulation(circuit); if (drawing) drawing->Refresh(false); update_waveform(); }
    void set_status(wchar_t const* message) { if (frame) frame->SetStatusText(message); }
    void update_controls();
    void remember_change();
    void undo();
    void clear_selection() { selectedComponents.clear(); selectedWires.clear(); }
    int single_component() const {
        return selectedComponents.size() == 1 && selectedWires.empty() ? *selectedComponents.begin() : 0;
    }
    void choose_tool(Tool next);
    void select_all();
    void select_rectangle(POINT first, POINT last);
    void delete_selected();
    void rotate_selected();
    void change_count(int delta);
    void change_direction();
    void toggle_selected();
    void reset_inputs();
    Component* component_at(POINT p);
    bool port_at(POINT p, Port& result);
    int wire_at(POINT p);
    void canvas_down(POINT p);
    void canvas_move(POINT p, bool leftDown);
    void canvas_up(POINT p);
    void paint_canvas(HDC dc, RECT client);
    void new_file();
    void open_file();
    void save_file(bool choosePath);
    void handle_command(int id, int notification);

};

App app;
wxBitmapBundle toolbar_bitmap(int index);
class LibraryItem : public wxTreeItemData {
public:
    Tool tool;
    explicit LibraryItem(Tool value) : tool(value) {}
};

// 顶部菜单栏位于 Windows 非客户区：这里只桥接悬停通知，窗口及菜单生命周期仍归 wxWidgets。
class LogisimFrame : public wxFrame {
public:
    LogisimFrame() : wxFrame(nullptr,wxID_ANY,L"\u7B80\u6613 Logisim - wxWidgets",wxDefaultPosition,wxSize(1440,900)) {}
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override {
        // 只自绘顶层菜单文字，以便独立放大字号；下拉菜单和键盘导航仍由原生菜单处理。
        if (message == WM_MEASUREITEM && !wParam) {
            auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if(item->CtlType==ODT_MENU && item->itemData>=1 && item->itemData<=6) {
                wxClientDC dc(this); dc.SetFont(app.menuFont);
                auto size=dc.GetTextExtent(GetMenuBar()->GetMenuLabelText(item->itemData-1));
                item->itemWidth=size.x+FromDIP(16); item->itemHeight=size.y+FromDIP(8); return TRUE;
            }
        } else if (message == WM_DRAWITEM && !wParam) {
            auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if(item->CtlType==ODT_MENU && item->itemData>=1 && item->itemData<=6) {
                int saved=SaveDC(item->hDC);
                bool active=(item->itemState&(ODS_SELECTED|ODS_HOTLIGHT))!=0;
                FillRect(item->hDC,&item->rcItem,GetSysColorBrush(active?COLOR_HIGHLIGHT:COLOR_MENUBAR));
                                // 原生菜单栏高度受系统度量限制，按实际可用高度收缩字体并保留上下各两像素。
                LOGFONTW font{};GetObjectW(app.menuFont.GetHFONT(),sizeof(font),&font);
                font.lfHeight=-std::min(std::abs(font.lfHeight),std::max(1L,item->rcItem.bottom-item->rcItem.top-4));
                HFONT fitted=CreateFontIndirectW(&font);SelectObject(item->hDC,fitted);SetBkMode(item->hDC,TRANSPARENT);
                SetTextColor(item->hDC,GetSysColor(active?COLOR_HIGHLIGHTTEXT:COLOR_MENUTEXT));
                wxString label=GetMenuBar()->GetMenuLabelText(item->itemData-1);
                RECT rect=item->rcItem; DrawTextW(item->hDC,label.wc_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                RestoreDC(item->hDC,saved);DeleteObject(fitted); return TRUE;
            }
        }
        if (message == WM_NCMOUSEMOVE && wParam == HTMENU) {
            POINT cursor{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            HMENU menu = GetMenu(GetHandle());
            for (int i=0;i<GetMenuItemCount(menu);++i) {
                RECT r{};
                if (GetMenuItemRect(GetHandle(),menu,i,&r) && PtInRect(&r,cursor)) {
                    static wchar_t const* help[]={L"\u65B0\u5EFA\u3001\u6253\u5F00\u3001\u4FDD\u5B58\u4E0E\u5BFC\u51FA\u7535\u8DEF",L"\u64A4\u9500\u3001\u9009\u62E9\u3001\u5220\u9664\u548C\u65CB\u8F6C",L"\u9009\u62E9\u8981\u653E\u7F6E\u7684\u5143\u4EF6",L"\u5207\u6362\u6216\u590D\u4F4D\u8F93\u5165\u4FE1\u53F7",L"\u653E\u5927\u3001\u7F29\u5C0F\u6216\u590D\u539F\u753B\u5E03",L"\u67E5\u770B\u64CD\u4F5C\u8BF4\u660E"};
                    if(i<6)app.show_hint(help[i],wxRect(r.left,r.top,r.right-r.left,r.bottom-r.top)); break;
                }
            }
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,GetHandle(),0}; TrackMouseEvent(&track);
        } else if (message == WM_NCMOUSELEAVE) app.show_hint(wxEmptyString);
        return wxFrame::MSWWindowProc(message,wParam,lParam);
    }
};

// 提示固定在对应控件或菜单项下沿，不随鼠标位置漂移；屏幕边缘处限制在工作区内。
void App::show_hint(wxString const& text, wxRect anchor) {
    if (!hint) return;
    if (text.empty() || anchor.IsEmpty()) { hint->Hide(); return; }
    hintLabel->SetLabel(text); hintLabel->Wrap(380); hint->Fit();
    wxPoint cursor=anchor.GetTopLeft(), pos(anchor.x,anchor.GetBottom()+4);
    int index=wxDisplay::GetFromPoint(cursor);
    if (index != wxNOT_FOUND) {
        wxRect work=wxDisplay(index).GetClientArea(); wxSize size=hint->GetSize();
        if(pos.y+size.y>work.GetBottom()) pos.y=cursor.y-size.y-8;
        pos.x=std::max(work.x,std::min(pos.x,work.GetRight()-size.x));
        pos.y=std::max(work.y,std::min(pos.y,work.GetBottom()-size.y));
    }
    hint->Move(pos); hint->Show();
}

// wxWidgets 原生控件、菜单与 sizer 三栏布局；保留放大字体，元件库占满左侧可用高度。
void App::create_ui() {
    frame=new LogisimFrame(); mainWindow=frame->GetHandle();
    frame->SetMinSize(wxSize(1050,650));
    frame->SetFont(wxFontInfo(14).FaceName("Microsoft YaHei UI"));
    uiFont=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    gateFont=CreateFontW(-31,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI Symbol");
    auto* menuBar=new wxMenuBar();
    auto* file=new wxMenu();
    file->Append(FileNew,L"\u65B0\u5EFA\u7A97\u53E3\tCtrl+N",L"\u6253\u5F00\u65B0\u7684\u7A7A\u767D\u7535\u8DEF");
    file->Append(FileOpen,L"\u6253\u5F00\tCtrl+O",L"\u6253\u5F00 JSON \u6216 .circ \u7535\u8DEF");
    file->Append(FileSave,L"\u4FDD\u5B58\tCtrl+S",L"\u4FDD\u5B58\u5F53\u524D\u7535\u8DEF");
    file->Append(FileSaveAs,L"\u53E6\u5B58\u4E3A / \u5BFC\u51FA\tCtrl+Shift+S",L"\u4FDD\u5B58\u7535\u8DEF\u6216\u5BFC\u51FA KiCad \u7F51\u8868");
    file->AppendSeparator(); file->Append(FileExit,L"\u9000\u51FA\tCtrl+Q");
    menuBar->Append(file,L"\u6587\u4EF6");
    auto* edit=new wxMenu();
    edit->Append(EditUndo,L"\u64A4\u9500\tCtrl+Z"); edit->Append(EditSelectAll,L"\u5168\u9009\tCtrl+A");
    edit->Append(EditDelete,L"\u5220\u9664\tDelete"); edit->Append(EditRotate,L"\u65CB\u8F6C\tR");
    menuBar->Append(edit,L"\u7F16\u8F91");
    auto* project=new wxMenu();
    for(int i=AndTool;i<=ConstantTool;++i) if(i!=RSLatchTool) project->Append(ToolBase+i,KindNames[i-AndTool]);
    menuBar->Append(project,L"\u5143\u4EF6");
    auto* simulation=new wxMenu(); simulation->Append(SimToggle,L"\u5207\u6362\u9009\u4E2D\u8F93\u5165"); simulation->Append(SimReset,L"\u6240\u6709\u8F93\u5165\u8BBE\u4E3A 0");
    menuBar->Append(simulation,L"\u6A21\u62DF");
    auto* view=new wxMenu(); view->Append(ZoomIn,L"\u653E\u5927\tCtrl++"); view->Append(ZoomOut,L"\u7F29\u5C0F\tCtrl+-"); view->Append(ZoomReset,L"\u590D\u539F\tCtrl+0"); menuBar->Append(view,L"\u89C6\u56FE");
    auto* help=new wxMenu(); help->Append(HelpAbout,L"\u4F7F\u7528\u8BF4\u660E"); menuBar->Append(help,L"\u5E2E\u52A9"); frame->SetMenuBar(menuBar);
    // 顶部菜单使用 13 磅字体，不改动系统全局菜单字体设置。
    menuFont=wxFont(wxFontInfo(13).FaceName("Microsoft YaHei UI"));
    for(int i=0;i<6;++i) {
        MENUITEMINFOW info{};info.cbSize=sizeof(info);info.fMask=MIIM_FTYPE|MIIM_DATA;
        info.fType=MFT_OWNERDRAW;info.dwItemData=i+1;SetMenuItemInfoW(GetMenu(mainWindow),i,TRUE,&info);
    }
    DrawMenuBar(mainWindow);
    frame->CreateStatusBar();
    hint=new wxPopupWindow(frame,wxBORDER_SIMPLE); hint->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
    hintLabel=new wxStaticText(hint,wxID_ANY,wxEmptyString); auto* hs=new wxBoxSizer(wxVERTICAL); hs->Add(hintLabel,0,wxALL,6); hint->SetSizer(hs);
    frame->Bind(wxEVT_MENU_HIGHLIGHT,[this](wxMenuEvent& e){
        auto* item=frame->GetMenuBar()->FindItem(e.GetMenuId()); RECT rect{};
        HMENU native=item?item->GetMenu()->GetHMenu():nullptr; int position=0;
        while(native && position<GetMenuItemCount(native) && GetMenuItemID(native,position)!=static_cast<UINT>(e.GetMenuId())) ++position;
        if(item && GetMenuItemRect(mainWindow,native,position,&rect))
            show_hint(item->GetHelp().empty()?item->GetItemLabelText():item->GetHelp(),wxRect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top));
        else show_hint(wxEmptyString);
    });
    frame->Bind(wxEVT_MENU_CLOSE,[this](wxMenuEvent&){show_hint(wxEmptyString);});
    frame->Bind(wxEVT_MENU,[this](wxCommandEvent& e){show_hint(wxEmptyString);handle_command(e.GetId(),0);});
    auto* panel=new wxPanel(frame); auto* vertical=new wxBoxSizer(wxVERTICAL); auto* toolbar=new wxBoxSizer(wxHORIZONTAL);
    for(int i=0;i<7;++i) {
        wxString label=i==0?L"\u9009\u62E9":i==1?L"\u8FDE\u7EBF":KindNames[i-AndTool];
        auto* button=new wxBitmapToggleButton(panel,ToolBase+i,toolbar_bitmap(i),wxDefaultPosition,panel->FromDIP(wxSize(66,42))); toolButtons[i]=button;
        button->SetName(label);
        button->Bind(wxEVT_ENTER_WINDOW,[this,button,label](wxMouseEvent& e){show_hint(label,button->GetScreenRect());e.Skip();});
        button->Bind(wxEVT_LEAVE_WINDOW,[this](wxMouseEvent& e){show_hint(wxEmptyString);e.Skip();});
         button->SetValue(i==SelectTool); button->Bind(wxEVT_TOGGLEBUTTON,[this,i](wxCommandEvent&){choose_tool(static_cast<Tool>(i));}); toolbar->Add(button,0,wxRIGHT,6);
    }
    vertical->Add(toolbar,0,wxEXPAND|wxALL,10); auto* columns=new wxBoxSizer(wxHORIZONTAL);
    auto* left=new wxBoxSizer(wxVERTICAL); left->Add(new wxStaticText(panel,wxID_ANY,L"\u5143\u4EF6\u5E93"),0,wxBOTTOM,8);
    tree=new wxTreeCtrl(panel,LibraryTree,wxDefaultPosition,wxSize(280,-1),wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_HIDE_ROOT|wxBORDER_SUNKEN);
    libraryTree=tree->GetHandle(); auto root=tree->AddRoot("root");
    auto category=[&](wchar_t const* name,std::initializer_list<Tool> tools){auto group=tree->AppendItem(root,name); for(auto t:tools)libraryItems[t]=tree->AppendItem(group,KindNames[t-AndTool],-1,-1,new LibraryItem(t)); return group;};
    auto gates=category(L"\u95E8\u7535\u8DEF",{AndTool,OrTool,NotTool,XorTool,NandTool,NorTool,XnorTool});
    category(L"\u8FD0\u7B97\u5668",{HalfAdderTool,HalfSubtractorTool,EqualTool});
    category(L"\u5F15\u811A",{InputTool,OutputTool,ClockTool,ConstantTool});
    // RS 不再提供新建入口，但保留旧文件中的 RS 元件、仿真和导出，避免工程丢失。
    category(L"\u89E6\u53D1\u5668",{DFlipFlopTool}); tree->Expand(gates);
    tree->Bind(wxEVT_TREE_SEL_CHANGED,[this](wxTreeEvent& e){if(updatingControls)return;auto* data=dynamic_cast<LibraryItem*>(tree->GetItemData(e.GetItem()));if(data)choose_tool(data->tool);});
    tree->Bind(wxEVT_TREE_ITEM_EXPANDING,[this,root](wxTreeEvent& e){wxTreeItemIdValue cookie;for(auto item=tree->GetFirstChild(root,cookie);item.IsOk();item=tree->GetNextChild(root,cookie))if(item!=e.GetItem())tree->Collapse(item);e.Skip();});
    // 树控件捕获拖动鼠标；只有在画布客户区内松开才真正创建元件，取消不写入撤销历史。
    tree->Bind(wxEVT_TREE_BEGIN_DRAG,[this](wxTreeEvent& e){
        auto* data=dynamic_cast<LibraryItem*>(tree->GetItemData(e.GetItem()));
        if(!data)return;
        libraryDragTool=data->tool; show_hint(wxEmptyString);
        if(!tree->HasCapture())tree->CaptureMouse();
        tree->SetCursor(wxCursor(wxCURSOR_HAND));
    });
    tree->Bind(wxEVT_MOTION,[this](wxMouseEvent& e){
        if(libraryDragTool==SelectTool){e.Skip();return;}
        auto point=drawing->ScreenToClient(tree->ClientToScreen(e.GetPosition()));
        tree->SetCursor(wxCursor(drawing->GetClientRect().Contains(point)?wxCURSOR_CROSS:wxCURSOR_NO_ENTRY));
    });
    tree->Bind(wxEVT_LEFT_UP,[this](wxMouseEvent& e){
        if(libraryDragTool==SelectTool){e.Skip();return;}
        Tool dropped=libraryDragTool; libraryDragTool=SelectTool;
        auto point=drawing->ScreenToClient(tree->ClientToScreen(e.GetPosition()));
        if(tree->HasCapture())tree->ReleaseMouse(); tree->SetCursor(wxNullCursor);
        if(drawing->GetClientRect().Contains(point)) {
            choose_tool(dropped);
            POINT world=screen_to_world(pt(point.x,point.y));canvas_down(world);canvas_up(world);
        }
        choose_tool(SelectTool);
    });
    tree->Bind(wxEVT_MOUSE_CAPTURE_LOST,[this](wxMouseCaptureLostEvent&){
        libraryDragTool=SelectTool;tree->SetCursor(wxNullCursor);choose_tool(SelectTool);
    });
    frame->Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& e){
        if(e.GetKeyCode()==WXK_ESCAPE && libraryDragTool!=SelectTool) {
            libraryDragTool=SelectTool;if(tree->HasCapture())tree->ReleaseMouse();
            tree->SetCursor(wxNullCursor);choose_tool(SelectTool);
        } else e.Skip();
    });
    left->Add(tree,1,wxEXPAND); columns->Add(left,0,wxEXPAND|wxRIGHT,10);
    drawing=new wxPanel(panel,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxBORDER_SUNKEN|wxWANTS_CHARS); canvas=drawing->GetHandle(); drawing->SetBackgroundStyle(wxBG_STYLE_PAINT);
    // 波形区位于画布下方，可横向查看采样步、纵向查看全部实际端口。
    auto* center=new wxBoxSizer(wxVERTICAL);center->Add(drawing,1,wxEXPAND);
    auto* waveHeader=new wxBoxSizer(wxHORIZONTAL);
    waveHeader->Add(new wxStaticText(panel,wxID_ANY,L"\u8FDE\u7EED\u6CE2\u5F62\uFF08100 ms\uFF09"),1,wxALIGN_CENTER_VERTICAL);
    auto* sampleButton=new wxButton(panel,wxID_ANY,L"\u6682\u505C");
    auto* clearButton=new wxButton(panel,wxID_ANY,L"\u6E05\u7A7A\u6CE2\u5F62");
    waveHeader->Add(sampleButton,0,wxRIGHT,4);waveHeader->Add(clearButton);
    center->Add(waveHeader,0,wxEXPAND|wxTOP|wxBOTTOM,4);
    waveform=new wxScrolledWindow(panel,wxID_ANY,wxDefaultPosition,panel->FromDIP(wxSize(-1,190)),wxBORDER_SUNKEN|wxHSCROLL|wxVSCROLL);
    waveform->SetBackgroundStyle(wxBG_STYLE_PAINT);waveform->SetScrollRate(20,20);
    waveform->Bind(wxEVT_PAINT,[this](wxPaintEvent&){paint_waveform();});
    // 定时采样保持平台连续；暂停只停止记录，不修改电路状态，也不自动翻转手动时钟。
    waveTimer=new wxTimer(frame);
    frame->Bind(wxEVT_TIMER,[this](wxTimerEvent&){advance_simulation(circuit);update_waveform(true);},waveTimer->GetId());
    sampleButton->Bind(wxEVT_BUTTON,[this,sampleButton](wxCommandEvent&){
        if(waveTimer->IsRunning()){waveTimer->Stop();sampleButton->SetLabel(L"\u7EE7\u7EED");}
        else {waveTimer->Start(100);sampleButton->SetLabel(L"\u6682\u505C");}
    });
    waveTimer->Start(100);
    clearButton->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){waves=WaveHistory{};waveform->Scroll(0,0);update_waveform();});
    center->Add(waveform,0,wxEXPAND);columns->Add(center,1,wxEXPAND|wxRIGHT,10);
    // Windows 版 wxPaintDC 提供 HDC，复用已有双缓冲绘图；鼠标、刷新和窗口销毁由 wxWidgets 分派。
    drawing->Bind(wxEVT_PAINT,[this](wxPaintEvent&){wxPaintDC dc(drawing);auto size=drawing->GetClientSize();paint_canvas(static_cast<HDC>(dc.GetHDC()),RECT{0,0,size.x,size.y});});
    drawing->Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent& e){drawing->SetFocus();canvas_down(screen_to_world(pt(e.GetX(),e.GetY())));});
    drawing->Bind(wxEVT_MOTION,[this](wxMouseEvent& e){show_hint(wxEmptyString);canvas_move(screen_to_world(pt(e.GetX(),e.GetY())),e.LeftIsDown());});
    drawing->Bind(wxEVT_LEFT_UP,[this](wxMouseEvent& e){canvas_up(screen_to_world(pt(e.GetX(),e.GetY())));});
    drawing->Bind(wxEVT_MOUSEWHEEL,[this](wxMouseEvent& e){auto p=drawing->ClientToScreen(e.GetPosition());wheel_zoom(pt(p.x,p.y),e.GetWheelRotation());});
    auto* right=new wxBoxSizer(wxVERTICAL); right->SetMinSize(wxSize(290,-1));
    right->Add(new wxStaticText(panel,wxID_ANY,L"\u5C5E\u6027"),0,wxBOTTOM,8);
    selection=new wxStaticText(panel,wxID_ANY,wxEmptyString);selectionLabel=selection->GetHandle();right->Add(selection,0,wxBOTTOM,14);
    auto* counts=new wxBoxSizer(wxHORIZONTAL);counts->Add(new wxStaticText(panel,wxID_ANY,L"\u8F93\u5165\u6570"),0,wxALIGN_CENTER_VERTICAL|wxRIGHT,8);
    auto* minus=new wxButton(panel,CountMinus,"-",wxDefaultPosition,wxSize(36,-1));countMinus=minus->GetHandle();counts->Add(minus);
    countBox=new wxTextCtrl(panel,CountEdit,"2",wxDefaultPosition,wxSize(54,-1),wxTE_CENTRE|wxTE_PROCESS_ENTER);countEdit=countBox->GetHandle();counts->Add(countBox,0,wxLEFT|wxRIGHT,4);
    auto* plus=new wxButton(panel,CountPlus,"+",wxDefaultPosition,wxSize(36,-1));countPlus=plus->GetHandle();counts->Add(plus);right->Add(counts,0,wxBOTTOM,12);
    minus->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){change_count(-1);});plus->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){change_count(1);});
    countBox->Bind(wxEVT_TEXT_ENTER,[this](wxCommandEvent&){change_count(0);});countBox->Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& e){if(!updatingControls)change_count(0);e.Skip();});
    wxArrayString names;for(auto n:DirectionNames)names.Add(n);directions=new wxChoice(panel,DirectionCombo,wxDefaultPosition,wxDefaultSize,names);directionCombo=directions->GetHandle();right->Add(directions,0,wxEXPAND|wxBOTTOM,12);
    directions->Bind(wxEVT_CHOICE,[this](wxCommandEvent&){change_direction();});
    auto* rotate=new wxButton(panel,RotateButton,L"\u987A\u65F6\u9488\u65CB\u8F6C  R");rotateButton=rotate->GetHandle();right->Add(rotate,0,wxEXPAND|wxBOTTOM,20);rotate->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){rotate_selected();});
    right->Add(new wxStaticText(panel,wxID_ANY,L"\u5F15\u811A\u8BF4\u660E / \u64CD\u4F5C\u63D0\u793A"),0,wxBOTTOM,8);
    helpText=new wxTextCtrl(panel,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxTE_MULTILINE|wxTE_READONLY|wxTE_WORDWRAP|wxBORDER_NONE);right->Add(helpText,1,wxEXPAND);
    columns->Add(right,0,wxEXPAND);vertical->Add(columns,1,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,10);panel->SetSizer(vertical);
    auto* outer=new wxBoxSizer(wxVERTICAL);outer->Add(panel,1,wxEXPAND);frame->SetSizer(outer);
    frame->Bind(wxEVT_CLOSE_WINDOW,[this](wxCloseEvent&){waveTimer->Stop();delete waveTimer;waveTimer=nullptr;show_hint(wxEmptyString);frame->Destroy();});
    frame->Centre();update_controls();set_status(L"\u9009\u62E9\u5143\u4EF6\u540E\u5728\u753B\u5E03\u4E0A\u653E\u7F6E\u3002");frame->Show();
}

// 固定间隔采样，不把鼠标刷新计为时间步。显示区自动跟随最新信号，端口名称固定在左侧。
void App::update_waveform(bool force) {
    if(!waveform)return;
    waves.capture(circuit,force,false);
    waveform->SetVirtualSize(wxSize(waveform->GetClientSize().x,waveform->FromDIP(50+static_cast<int>(waves.labels.size())*40)));
    waveform->Refresh(false);
}
void App::paint_waveform() {
    wxAutoBufferedPaintDC dc(waveform);waveform->DoPrepareDC(dc);
    dc.SetBackground(*wxWHITE_BRUSH);dc.Clear();dc.SetFont(wxFont(wxFontInfo(10)));
    auto dip=[this](int n){return waveform->FromDIP(n);};
    dc.SetTextForeground(wxColour(70,70,70));
    if(waves.labels.empty()){dc.DrawText(L"\u6DFB\u52A0\u5143\u4EF6\u540E\u663E\u793A\u5F15\u811A\u6CE2\u5F62",dip(8),dip(8));return;}
    // 每段 12 DIP、每十次采样标一秒；超出可见宽度后滚动时间窗口，避免标签随波形移走。
    size_t capacity=std::max(1,(waveform->GetClientSize().x-dip(150))/dip(12));
    size_t first=waves.samples.size()>capacity?waves.samples.size()-capacity:0;
    for(size_t t=first;t<waves.samples.size();++t) {
        if((waves.firstStep+t)%10)continue;
        int x=dip(140+static_cast<int>(t-first)*12);dc.SetPen(wxPen(wxColour(230,235,240)));
        dc.DrawLine(x,0,x,dip(40+static_cast<int>(waves.labels.size())*40));
        dc.DrawText(wxString::Format("%.1f s",(waves.firstStep+t)*0.1),x,dip(4));
    }
    for(size_t row=0;row<waves.labels.size();++row) {
        int top=36+static_cast<int>(row)*40;
        dc.SetTextForeground(wxColour(40,50,60));dc.DrawText(waves.labels[row],dip(8),dip(top));
        dc.SetTextForeground(wxColour(120,120,120));dc.DrawText("1",dip(122),dip(top-5));dc.DrawText("0",dip(122),dip(top+15));
        dc.SetPen(wxPen(wxColour(22,132,100),dip(2)));
        for(size_t t=first;t<waves.samples.size();++t) {
            int x=140+static_cast<int>(t-first)*12,y=top+(waves.samples[t][row]?0:20);
            if(t>first)dc.DrawLine(dip(x),dip(top+(waves.samples[t-1][row]?0:20)),dip(x),dip(y));
            dc.DrawLine(dip(x),dip(y),dip(x+12),dip(y));
        }
    }
}
// 把画布客户区坐标转换为逻辑坐标，消除缩放和平移对命中检测与放置位置的影响。
POINT App::screen_to_world(POINT point) const {
    return pt(static_cast<int>(std::lround((point.x - viewX) * 100.0 / zoomPercent)),
              static_cast<int>(std::lround((point.y - viewY) * 100.0 / zoomPercent)));
}

// 以指定屏幕点为缩放锚点，调整平移量，避免鼠标下方的电路在缩放时跳动。
void App::zoom_at(int direction, POINT anchor) {
    static constexpr int levels[] = {50, 75, 100, 125, 150, 200, 250, 300};
    int next = zoomPercent;
    if (direction > 0) {
        auto level = std::upper_bound(std::begin(levels), std::end(levels), zoomPercent);
        if (level != std::end(levels)) next = *level;
    } else if (direction < 0) {
        auto level = std::lower_bound(std::begin(levels), std::end(levels), zoomPercent);
        if (level != std::begin(levels)) next = *--level;
    } else next = 100;
    if (next == zoomPercent) return;
    double worldX = (anchor.x - viewX) * 100.0 / zoomPercent;
    double worldY = (anchor.y - viewY) * 100.0 / zoomPercent;
    zoomPercent = next;
    viewX = anchor.x - static_cast<int>(std::lround(worldX * zoomPercent / 100.0));
    viewY = anchor.y - static_cast<int>(std::lround(worldY * zoomPercent / 100.0));
    std::wstring message = L"\u753B\u5E03\u7F29\u653E " + std::to_wstring(zoomPercent) + L"%\uFF1B\u6EDA\u8F6E\u6216 Ctrl+\u52A0\u51CF\u53F7\u53EF\u8C03\u6574\uFF0CCtrl+0 \u590D\u539F\u3002";
    set_status(message.c_str());
    refresh();
}

void App::zoom_center(int direction) {
    RECT rect{};
    GetClientRect(canvas, &rect);
    zoom_at(direction, pt(rect.right / 2, rect.bottom / 2));
}

bool App::wheel_zoom(POINT screen, int delta) {
    POINT anchor = screen;
    ScreenToClient(canvas, &anchor);
    RECT rect{};
    GetClientRect(canvas, &rect);
    if (!PtInRect(&rect, anchor)) return false;
    wheelRemainder += delta;
    while (wheelRemainder >= WHEEL_DELTA) {
        zoom_at(1, anchor);
        wheelRemainder -= WHEEL_DELTA;
    }
    while (wheelRemainder <= -WHEEL_DELTA) {
        zoom_at(-1, anchor);
        wheelRemainder += WHEEL_DELTA;
    }
    return true;
}

// 根据选中元件更新属性栏；只有支持可变输入数的组合门才允许增减输入端口。
void App::update_controls() {
    if (!selectionLabel) return;
    int count = static_cast<int>(selectedComponents.size() + selectedWires.size());
    int single = single_component();
    Component const* component = find_component(circuit, single);
    std::wstring label = count > 1 ? L"\u5DF2\u9009\u4E2D " + std::to_wstring(count) + L" \u9879" :
                         component ? KindNames[component->kind] : count == 1 ? L"\u5BFC\u7EBF" : L"\u672A\u9009\u4E2D\u5143\u4EF6";
    selection->SetLabel(label);
    // 选中 D 或准备放置 D 时均显示端口说明；旧工程 RS 仍有明确的引脚说明。
    Kind kind=component?component->kind:tool>=AndTool?static_cast<Kind>(tool-AndTool):And;
    helpText->ChangeValue(kind==DFlipFlop ?
        L"D\uFF1A\u6570\u636E\u8F93\u5165\nCLK\uFF1A\u4E0A\u5347\u6CBF\u91C7\u6837 D\nSET\uFF1A\u9AD8\u7535\u5E73\u5F02\u6B65\u7F6E\u4F4D\nCLR\uFF1A\u9AD8\u7535\u5E73\u5F02\u6B65\u6E05\u96F6\nQ\uFF1A\u5B58\u50A8\u7684\u72B6\u6001\nQ \u4E0A\u6A2A\u7EBF\uFF1A\u53CD\u76F8\u8F93\u51FA\n\nSET=CLR=1 \u65F6 CLR \u4F18\u5148\u3002\n\u672A\u8FDE\u63A5\u8F93\u5165\u6309 0 \u5904\u7406\u3002" : kind==RSLatch ?
        L"S\uFF1A\u9AD8\u7535\u5E73\u7F6E\u4F4D\nR\uFF1A\u9AD8\u7535\u5E73\u590D\u4F4D\uFF08\u4F18\u5148\uFF09\nQ / \u53CD\u76F8 Q\uFF1A\u4E92\u8865\u8F93\u51FA\n\n\u4EC5\u4E3A\u517C\u5BB9\u65E7\u5DE5\u7A0B\u4FDD\u7559 RS\u3002" :
        L"Ctrl+Z \u64A4\u9500\nCtrl+A \u5168\u9009\nDelete \u5220\u9664\nR \u65CB\u8F6C\n\u62D6\u52A8\u6846\u9009\n\u5355\u51FB\u8F93\u5165\u5207\u6362 0/1\n\u6EDA\u8F6E\u7F29\u653E\nCtrl+0 \u590D\u539F");
    bool gate = component && adjustable_gate(component->kind);
    countBox->Enable(gate); frame->FindWindow(CountMinus)->Enable(gate); frame->FindWindow(CountPlus)->Enable(gate);
    directions->Enable(component != nullptr); frame->FindWindow(RotateButton)->Enable(component != nullptr);
    if (component) {
        updatingControls = true;
        countBox->ChangeValue(std::to_wstring(input_count(*component)));
        directions->SetSelection(component->direction);
        updatingControls = false;
    }
}

// 修改前保存电路和选择状态快照，撤销时一并恢复；拖动过程避免为每个鼠标事件重复记录。
void App::remember_change() {
    undoHistory.push_back({circuit, selectedComponents, selectedWires});
}

void App::undo() {
    if (undoHistory.empty()) { set_status(L"\u6CA1\u6709\u53EF\u64A4\u9500\u7684\u64CD\u4F5C\u3002"); return; }
    UndoState previous = std::move(undoHistory.back());
    undoHistory.pop_back();
    circuit = std::move(previous.circuit);
    selectedComponents = std::move(previous.selectedComponents);
    selectedWires = std::move(previous.selectedWires);
    if (canvas && GetCapture() == canvas) ReleaseCapture();
    draggingComponent = dragged = marquee = dragUndoRecorded = false;
    wireStartId = branchFromWireId = 0; wireStartPort = -1;
    bends.clear(); hoverValid = false;
    update_controls(); refresh();
    set_status(L"\u5DF2\u64A4\u9500\u4E0A\u4E00\u6B21\u64CD\u4F5C\u3002");
}


void App::choose_tool(Tool next) {
    tool = next;
    if (tree) {
        updatingControls=true;
        auto item=libraryItems.find(next);
        if(item!=libraryItems.end()) tree->SelectItem(item->second); else tree->UnselectAll();
        updatingControls=false;
    }
    if (drawing) drawing->SetCursor(wxCursor(next==SelectTool?wxCURSOR_ARROW:wxCURSOR_CROSS));
    wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); hoverValid = false;
    marquee = draggingComponent = false;
    if (next != SelectTool) clear_selection();
    // 图形工具按钮互斥选中；从菜单或元件库切换工具时也同步按下状态。
    for (int i=0;i<7;++i) if(toolButtons[i]) toolButtons[i]->SetValue(i==next);
    SetCursor(LoadCursorW(nullptr, next == SelectTool ? IDC_ARROW : IDC_CROSS));
    set_status(next == SelectTool ? L"\u9009\u62E9\u5DE5\u5177\uFF1B\u4ECE\u7A7A\u767D\u5904\u62D6\u52A8\u53EF\u6846\u9009\u3002" :
               next == WireTool ? L"\u5355\u51FB\u8F93\u51FA\u7AEF\u6216\u5DF2\u6709\u5BFC\u7EBF\u5F00\u59CB\uFF0C\u518D\u5355\u51FB\u8F93\u5165\u7AEF\uFF1B\u7A7A\u767D\u5904\u53EF\u6DFB\u52A0\u6298\u70B9\u3002" :
                                  L"\u5728\u753B\u5E03\u4E0A\u5355\u51FB\u4EE5\u653E\u7F6E\u5143\u4EF6\u3002");
    update_controls(); refresh();
}

void App::select_all() {
    choose_tool(SelectTool);
    selectedComponents.clear(); selectedWires.clear();
    for (auto const& c : circuit.components) selectedComponents.insert(c.id);
    for (auto const& w : circuit.wires) selectedWires.insert(w.id);
    update_controls(); refresh();
}

void App::select_rectangle(POINT first, POINT last) {
    RECT rect{std::min(first.x, last.x), std::min(first.y, last.y),
              std::max(first.x, last.x), std::max(first.y, last.y)};
    clear_selection();
    for (auto const& c : circuit.components)
        if (intersects(component_bounds(c), rect)) selectedComponents.insert(c.id);
    for (auto const& w : circuit.wires)
        if (wire_intersects_rect(route_wire(circuit, w), rect)) selectedWires.insert(w.id);
    update_controls(); refresh();
}

void App::delete_selected() {
    if (selectedComponents.empty() && selectedWires.empty()) return;
    remember_change();
    std::set<int> removed = selectedWires;
    for (auto const& wire : circuit.wires)
        if (selectedComponents.count(wire.source) || selectedComponents.count(wire.target))
            removed.insert(wire.id);
    remove_wires(circuit, removed);
    circuit.components.erase(std::remove_if(circuit.components.begin(), circuit.components.end(), [&](Component const& c) {
        return selectedComponents.count(c.id) != 0;
    }), circuit.components.end());
    clear_selection(); update_controls(); refresh();
}

void App::rotate_selected() {
    Component* component = find_component(circuit, single_component());
    if (!component) return;
    remember_change();
    set_component_direction(circuit, component->id, (component->direction + 1) % 4);
    update_controls(); refresh();
}

void App::change_count(int delta) {
    if (updatingControls) return;
    Component* component = find_component(circuit, single_component());
    if (!component || !adjustable_gate(component->kind)) return;
    wchar_t buffer[32]{}; GetWindowTextW(countEdit, buffer, 32);
    int count = _wtoi(buffer);
    if (delta != 0) count = component->inputs + delta;
    if (count < 2 || count > 8) { update_controls(); return; }
    if (count == component->inputs) return;
    remember_change();
    component->inputs = count;
    std::set<int> removed;
    for (auto const& wire : circuit.wires)
        if (wire.target == component->id && wire.port >= count) removed.insert(wire.id);
    remove_wires(circuit, removed);
    update_controls(); refresh();
}

void App::change_direction() {
    if (updatingControls) return;
    Component* component = find_component(circuit, single_component());
    if (!component) return;
    int direction = directions->GetSelection();
    if (direction >= 0 && direction < 4 && direction != component->direction) {
        remember_change();
        set_component_direction(circuit, component->id, direction);
    }
    refresh();
}

// 单击或命令切换输入、手动时钟和逻辑常量；手动时钟的 0 到 1 变化驱动 D 触发器采样。
void App::toggle_selected() {
    Component* component = find_component(circuit, single_component());
    if (component && is_source(component->kind)) {
        remember_change();
        component->value = !component->value; refresh();
    }
}

// 清零信号源和保存的时序状态，然后重新传播；如果电路连线仍满足置位条件，传播后状态可能再次改变。
void App::reset_inputs() {
    bool changed = std::any_of(circuit.components.begin(), circuit.components.end(),
        [](Component const& c) { return (is_source(c.kind) || sequential(c.kind)) && (c.value || c.lastClock); });
    if (!changed) return;
    remember_change();
    for (auto& c : circuit.components) if (is_source(c.kind) || sequential(c.kind)) { c.value = false; c.lastClock = false; }
    refresh();
}

Component* App::component_at(POINT p) {
    for (auto it = circuit.components.rbegin(); it != circuit.components.rend(); ++it) {
        auto const& c = *it;
        bool hit = false;
        if (is_source(c.kind) || c.kind == Output) {
            int dx = p.x - c.x, dy = p.y - c.y;
            hit = dx * dx + dy * dy <= 22 * 22;
        } else {
            RECT bounds = component_bounds(c);
            hit = p.x >= bounds.left && p.x <= bounds.right && p.y >= bounds.top && p.y <= bounds.bottom;
        }
        if (hit) return &*it;
    }
    return nullptr;
}

bool App::port_at(POINT p, Port& result) {
    int radius = std::max(3, (900 + zoomPercent - 1) / zoomPercent);
    for (auto it = circuit.components.rbegin(); it != circuit.components.rend(); ++it)
        for (auto const& port : ports(*it)) {
            int dx = p.x - port.point.x, dy = p.y - port.point.y;
            if (dx * dx + dy * dy <= radius * radius) { result = port; return true; }
        }
    return false;
}

int App::wire_at(POINT p) {
    int tolerance = std::max(2, (600 + zoomPercent - 1) / zoomPercent);
    for (auto it = circuit.wires.rbegin(); it != circuit.wires.rend(); ++it)
        if (near_polyline(route_wire(circuit, *it), p, tolerance)) return it->id;
    return 0;
}

// 画布按下事件：按当前工具开始放置、连线、选择、框选或拖动；连线记录逻辑端口而非仅记录坐标。
void App::canvas_down(POINT p) {
    SetFocus(canvas); SetCapture(canvas);
    press = p; dragged = dragUndoRecorded = false;
    if (tool == WireTool) {
        Port port{};
        bool found = port_at(p, port);
        if (!wireStartId) {
            if (found && port.index < 0) {
                wireStartId = port.component;
                wireStartPort = port.index;
                branchFromWireId = 0;
                bends.clear(); hoverValid = false;
            } else {
                int parentId = wire_at(p);
                auto const* parent = find_wire(circuit, parentId);
                if (parent) {
                    auto route = route_wire(circuit, *parent);
                    if (route.size() >= 2) {
                        wireStartId = parent->source;
                        wireStartPort = parent->sourcePort;
                        branchFromWireId = parentId;
                        branchPoint = nearest_route_point(route, p);
                        bends.clear(); hoverValid = false;
                        set_status(L"\u5DF2\u4ECE\u5BFC\u7EBF\u5F00\u59CB\u5206\u652F\uFF1B\u5355\u51FB\u8F93\u5165\u7AEF\u5B8C\u6210\u8FDE\u63A5\u3002");
                    }
                }
            }
        } else if (found && port.index >= 0) {
            if (wireStartId != port.component) {
                std::set<int> replaced;
                for (auto const& wire : circuit.wires)
                    if (wire.target == port.component && wire.port == port.index) replaced.insert(wire.id);
                bool replacingSource = false;
                for (int parentId = branchFromWireId; parentId;) {
                    if (replaced.count(parentId)) { replacingSource = true; break; }
                    auto const* parent = find_wire(circuit, parentId);
                    parentId = parent ? parent->parentWire : 0;
                }
                if (!replacingSource) {
                    remember_change();
                    remove_wires(circuit, replaced);
                    Wire wire{circuit.nextWire++, wireStartId,
                        port.component, port.index, bends, branchFromWireId, branchPoint};
                    wire.sourcePort = wireStartPort;
                    circuit.wires.push_back(wire);
                    set_status(L"\u5DF2\u8FDE\u63A5\uFF1B\u84DD\u8272\u8868\u793A\u771F\uFF0C\u9ED1\u8272\u8868\u793A\u5047\u3002");
                }
            }
            wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); hoverValid = false;
        } else if (!found) {
            bends.push_back(pt(snap(p.x), snap(p.y)));
        }
        refresh(); return;
    }
    if (tool != SelectTool) {
        if (!component_at(p)) {
            remember_change();
            Component c;
            c.id = circuit.nextComponent++;
            c.kind = static_cast<Kind>(tool - AndTool);
            c.x = snap(p.x); c.y = snap(p.y);
            c.inputs = c.kind == Not ? 1 : 2;
            circuit.components.push_back(c);
            clear_selection(); selectedComponents.insert(c.id);
            update_controls(); refresh();
        }
        return;
    }
    Component* component = component_at(p);
    if (component) {
        if (!selectedComponents.count(component->id)) {
            clear_selection(); selectedComponents.insert(component->id);
        }
        if (single_component()) {
            draggingComponent = true;
            dragComponentId = component->id;
            dragStartPosition = pt(component->x, component->y);
            dragOffset = pt(p.x - component->x, p.y - component->y);
        }
    } else {
        int wire = wire_at(p);
        if (wire) {
            if (selectedWires.size() == 1 && selectedWires.count(wire) && selectedComponents.empty())
                clear_selection();
            else if (!selectedWires.count(wire)) { clear_selection(); selectedWires.insert(wire); }
        } else {
            clear_selection();
            marquee = true;
            marqueeStart = marqueeEnd = p;
        }
    }
    update_controls(); refresh();
}

void App::canvas_move(POINT p, bool leftDown) {
    if (tool == WireTool && wireStartId) { hover = p; hoverValid = true; refresh(); return; }
    if (tool != SelectTool || !leftDown) return;
    if (marquee) { marqueeEnd = p; refresh(); return; }
    if (!draggingComponent || !single_component()) return;
    Component* component = find_component(circuit, dragComponentId);
    if (!component) return;
    int dx = p.x - press.x, dy = p.y - press.y;
    if (dx * dx + dy * dy > 16) dragged = true;
    int x = snap(p.x - dragOffset.x), y = snap(p.y - dragOffset.y);
    if ((x != component->x || y != component->y) && !dragUndoRecorded) {
        remember_change();
        dragUndoRecorded = true;
    }
    component->x = x;
    component->y = y;
    refresh();
}

// 结束拖动或框选；未发生拖动的信号源点击按电平切换处理。
void App::canvas_up(POINT p) {
    if (GetCapture() == canvas) ReleaseCapture();
    if (dragUndoRecorded) {
        Component const* component = find_component(circuit, dragComponentId);
        if (component && component->x == dragStartPosition.x && component->y == dragStartPosition.y)
            undoHistory.pop_back();
    }
    if (tool == SelectTool && marquee) {
        int dx = p.x - marqueeStart.x, dy = p.y - marqueeStart.y;
        marquee = false;
        if (dx * dx + dy * dy > 16) select_rectangle(marqueeStart, p);
    } else if (tool == SelectTool && draggingComponent && !dragged) {
        Component* component = find_component(circuit, dragComponentId);
        if (component && is_source(component->kind)) {
            int dx = p.x - component->x, dy = p.y - component->y;
            if (dx * dx + dy * dy <= 22 * 22) {
                remember_change();
                component->value = !component->value;
            }
        }
    }
    draggingComponent = dragUndoRecorded = false;
    refresh();
}

void draw_line(HDC dc, std::vector<POINT> const& points, COLORREF color, bool dashed, int width = 3) {
    if (points.size() < 2) return;
    HPEN pen = CreatePen(dashed ? PS_DASH : PS_SOLID, dashed ? 1 : width, color);
    HGDIOBJ previous = SelectObject(dc, pen);
    Polyline(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, previous); DeleteObject(pen);
}

void draw_shape(HDC dc, RECT bounds, bool ellipse, COLORREF fill, COLORREF border, bool dashed, bool square = false) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(dashed ? PS_DASH : PS_SOLID, dashed ? 1 : 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    if (ellipse) Ellipse(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    else if (square) Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    else RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, 12, 12);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

void gate_bezier(HDC dc, POINT center, int direction,
                 POINT control1, POINT control2, POINT end) {
    POINT points[3] = {
        gate_point(center, direction, control1.x, control1.y),
        gate_point(center, direction, control2.x, control2.y),
        gate_point(center, direction, end.x, end.y)
    };
    PolyBezierTo(dc, points, 3);
}

int gate_input_edge(Kind kind, int across, int height, int radius) {
    if (kind != Or && kind != Nor && kind != Xor && kind != Xnor) return -radius;
    double fraction = static_cast<double>(across) / std::max(1, height);
    return -radius + static_cast<int>(std::round(radius * 0.48 * (1.0 - fraction * fraction)));
}

void draw_gate_body(HDC dc, Kind kind, POINT center, int direction,
                    int radius, int height, COLORREF fill, COLORREF border, bool selected) {
    if (sequential(kind)) {
        // 矩形触发器：文字放在框内，与引线分离；旋转只改变位置，文字保持正向可读。
        int hx=direction%2?80:60, hy=direction%2?60:80;
        draw_shape(dc,RECT{center.x-hx,center.y-hy,center.x+hx,center.y+hy},false,fill,border,selected,true);
        auto label=[&](wchar_t const* text,int x,int y,bool bar=false) {
            POINT p=gate_point(center,direction,x,y);
            RECT r{p.x-24,p.y-14,p.x+24,p.y+14};
            auto old=SelectObject(dc,app.uiFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Dark);
            DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(bar)draw_line(dc,{pt(p.x-9,p.y-14),pt(p.x+9,p.y-14)},Dark,false,2);
            SelectObject(dc,old);
        };
        label(kind==DFlipFlop?L"D":L"S",-38,-40);
        label(L"Q",38,-40);label(L"Q",38,40,true);
        if(kind==DFlipFlop) {
            label(L"SET",0,-60);label(L"CLR",0,60);
            // 左侧小三角表示 CLK 上升沿触发，不使用容易遮挡引线的外部文字。
            draw_line(dc,{gate_point(center,direction,-60,10),gate_point(center,direction,-50,20),gate_point(center,direction,-60,30)},Dark,false,2);
            label(L"CLK",-23,20);
        } else label(L"R",-38,40);
        return;
    }
    if (kind >= HalfAdder) {
        int hx = sequential(kind) && direction % 2 ? height : radius;
        int hy = sequential(kind) && direction % 2 ? radius : height;
        draw_shape(dc, RECT{center.x-hx, center.y-hy, center.x+hx, center.y+hy}, false, fill, border, selected);
        RECT label{center.x - radius, center.y - height, center.x + radius, center.y + height};
        HGDIOBJ oldFont = SelectObject(dc, app.uiFont);
        int oldMode = SetBkMode(dc, TRANSPARENT);
        COLORREF oldColor = SetTextColor(dc, border);
        DrawTextW(dc, kind == HalfAdder ? L"\u03A3" : kind == HalfSubtractor ? L"\u2212" : kind == DFlipFlop ? L"D" : kind == RSLatch ? L"RS" : L"=",
                  -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, oldColor); SetBkMode(dc, oldMode); SelectObject(dc, oldFont);
        return;
    }
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(selected ? PS_DASH : PS_SOLID, selected ? 1 : 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    BeginPath(dc);
    POINT start = gate_point(center, direction, -radius, -height);
    MoveToEx(dc, start.x, start.y, nullptr);
    if (kind == And || kind == Nand) {
        POINT top = gate_point(center, direction, 0, -height);
        LineTo(dc, top.x, top.y);
        gate_bezier(dc, center, direction,
                    pt(radius * 3 / 5, -height), pt(radius, -height * 3 / 5), pt(radius, 0));
        gate_bezier(dc, center, direction,
                    pt(radius, height * 3 / 5), pt(radius * 3 / 5, height), pt(0, height));
        POINT bottom = gate_point(center, direction, -radius, height);
        LineTo(dc, bottom.x, bottom.y);
    } else if (kind == Or || kind == Nor || kind == Xor || kind == Xnor) {
        gate_bezier(dc, center, direction,
                    pt(-radius / 3, -height), pt(radius * 2 / 3, -height * 3 / 5), pt(radius, 0));
        gate_bezier(dc, center, direction,
                    pt(radius * 2 / 3, height * 3 / 5), pt(-radius / 3, height), pt(-radius, height));
        gate_bezier(dc, center, direction,
                    pt(-radius / 2, height / 2), pt(-radius / 2, -height / 2), pt(-radius, -height));
    } else {
        POINT tip = gate_point(center, direction, radius - 7, 0);
        POINT bottom = gate_point(center, direction, -radius, height);
        LineTo(dc, tip.x, tip.y);
        LineTo(dc, bottom.x, bottom.y);
    }
    CloseFigure(dc);
    EndPath(dc);
    StrokeAndFillPath(dc);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
    if (kind == Xor || kind == Xnor) {
        HPEN arc = CreatePen(selected ? PS_DASH : PS_SOLID, selected ? 1 : 2, border);
        HGDIOBJ oldArc = SelectObject(dc, arc);
        POINT top = gate_point(center, direction, -radius - 6, -height);
        MoveToEx(dc, top.x, top.y, nullptr);
        gate_bezier(dc, center, direction,
                    pt(-radius - 1, -height / 2), pt(-radius - 1, height / 2),
                    pt(-radius - 6, height));
        SelectObject(dc, oldArc); DeleteObject(arc);
    }
    if (inverted_gate(kind)) {
        POINT bubble = gate_point(center, direction, radius - 1, 0);
        draw_shape(dc, RECT{bubble.x - 5, bubble.y - 5, bubble.x + 5, bubble.y + 5},
                   true, RGB(255, 255, 255), border, selected);
    }
}


// 按原工具栏绘制箭头、导线、逻辑门和输入/输出图标；位图内嵌生成，不依赖外部图片文件。
// 图标缩小到 48×30 逻辑像素，提供 1x/2x 位图适配 DPI；透明背景保留原生按钮状态。
wxBitmapBundle toolbar_bitmap(int index) {
    auto render=[&](int scale) {
        wxBitmap bitmap(48*scale,30*scale,24);
        wxMemoryDC memory(bitmap);
        wxColour transparent(240,240,240);
        memory.SetBackground(wxBrush(transparent)); memory.Clear();
        HDC dc=static_cast<HDC>(memory.GetHDC()); int saved=SaveDC(dc);
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,80,48,nullptr);SetViewportExtEx(dc,48*scale,30*scale,nullptr);
        int cx=40,cy=24;
    if (index == SelectTool) {
        POINT arrow[] = {pt(cx - 15, cy - 20), pt(cx - 13, cy + 16), pt(cx - 5, cy + 7),
                         pt(cx + 2, cy + 20), pt(cx + 9, cy + 16), pt(cx + 2, cy + 4),
                         pt(cx + 15, cy + 2)};
        HBRUSH brush = CreateSolidBrush(Dark);
        HPEN pen = CreatePen(PS_SOLID, 1, Dark);
        HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        Polygon(dc, arrow, static_cast<int>(sizeof(arrow) / sizeof(arrow[0])));
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(brush); DeleteObject(pen);
    } else if (index == WireTool) {
        draw_line(dc, {pt(cx - 23, cy + 17), pt(cx + 23, cy - 17)}, Dark, false, 3);
    } else if (index <= NotTool) {
        Kind kind = static_cast<Kind>(index - AndTool);
        POINT center = pt(cx, cy);
        draw_gate_body(dc, kind, center, 0, 19, 17, RGB(255, 255, 255), Dark, false);
        auto offsets = kind == Not ? std::vector<int>{0} : std::vector<int>{-9, 9};
        for (int across : offsets) {
            int edge = gate_input_edge(kind, across, 17, 19);
            draw_line(dc, {pt(cx - 32, cy + across), pt(cx + edge, cy + across)}, Dark, false, 2);
        }
        draw_line(dc, {pt(cx + (kind == Not ? 23 : 19), cy), pt(cx + 33, cy)}, Dark, false, 2);
    } else if (index == InputTool) {
        draw_shape(dc, RECT{cx - 20, cy - 12, cx + 5, cy + 12}, false,
                   RGB(232, 244, 255), Dark, false);
        draw_line(dc, {pt(cx + 5, cy), pt(cx + 27, cy)}, Dark, false, 2);
    } else {
        draw_line(dc, {pt(cx - 27, cy), pt(cx - 8, cy)}, Dark, false, 2);
        draw_shape(dc, RECT{cx - 8, cy - 12, cx + 16, cy + 12}, true,
                   RGB(255, 255, 255), Dark, false);
    }

        RestoreDC(dc,saved);memory.SelectObject(wxNullBitmap);
        bitmap.SetMask(new wxMask(bitmap,transparent));
        return bitmap;
    };
    return wxBitmapBundle::FromBitmaps(render(1),render(2));
}

void App::paint_canvas(HDC dc, RECT client) {
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1L, client.right), std::max(1L, client.bottom));
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    FillRect(memory, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetMapMode(memory, MM_ANISOTROPIC);
    SetWindowExtEx(memory, 100, 100, nullptr);
    SetViewportExtEx(memory, zoomPercent, zoomPercent, nullptr);
    SetViewportOrgEx(memory, viewX, viewY, nullptr);
    POINT topLeft = screen_to_world(pt(0, 0));
    POINT bottomRight = screen_to_world(pt(client.right, client.bottom));
    int left = static_cast<int>(std::floor(static_cast<double>(topLeft.x) / Grid)) * Grid;
    int top = static_cast<int>(std::floor(static_cast<double>(topLeft.y) / Grid)) * Grid;
    int right = bottomRight.x + Grid;
    int bottom = bottomRight.y + Grid;
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(238, 242, 245));
    HGDIOBJ oldPen = SelectObject(memory, gridPen);
    for (int x = left; x <= right; x += Grid) { MoveToEx(memory, x, top, nullptr); LineTo(memory, x, bottom); }
    for (int y = top; y <= bottom; y += Grid) { MoveToEx(memory, left, y, nullptr); LineTo(memory, right, y); }
    SelectObject(memory, oldPen); DeleteObject(gridPen);
    auto values = signals(circuit);
    for (auto const& wire : circuit.wires)
        draw_line(memory, route_wire(circuit, wire), wire_value(values, wire) ? Blue : Dark,
                  selectedWires.count(wire.id) != 0);
    for (auto const& wire : circuit.wires) {
        if (!wire.parentWire) continue;
        auto const* parent = find_wire(circuit, wire.parentWire);
        if (!parent) continue;
        auto route = route_wire(circuit, *parent);
        if (route.empty()) continue;
        POINT tap = nearest_route_point(route, wire.tap);
        COLORREF color = wire_value(values, wire) ? Blue : Dark;
        draw_shape(memory, RECT{tap.x - 4, tap.y - 4, tap.x + 5, tap.y + 5},
                   true, color, color, false);
    }
    if (wireStartId && hoverValid) {
        auto const* source = find_component(circuit, wireStartId);
        if (source) {
            POINT tap{};
            POINT const* start = nullptr;
            if (branchFromWireId) {
                auto const* parent = find_wire(circuit, branchFromWireId);
                if (parent) {
                    auto route = route_wire(circuit, *parent);
                    if (!route.empty()) {
                        tap = nearest_route_point(route, branchPoint);
                        start = &tap;
                    }
                }
            }
            draw_line(memory, route_points(circuit, *source, nullptr, 0, bends, &hover, start, wireStartPort),
                      RGB(130, 168, 204), true, 2);
        }
    }
    SetBkMode(memory, TRANSPARENT);
    for (auto const& c : circuit.components) {
        bool selected = selectedComponents.count(c.id) != 0;
        bool value = values[c.id][0];
        COLORREF border = selected ? Blue : RGB(81, 97, 112);
        COLORREF fill = value && (is_source(c.kind) || c.kind == Output) ? RGB(220, 239, 255) :
                        selected ? RGB(234, 244, 255) : RGB(249, 251, 253);
        RECT bounds = component_bounds(c);
        if (is_source(c.kind) || c.kind == Output) {
            draw_shape(memory, bounds, c.kind == Output, fill, border, selected);
            RECT label = bounds;
            SelectObject(memory, gateFont);
            SetTextColor(memory, value ? Blue : Dark);
            DrawTextW(memory, c.kind == Clock ? (value ? L"\u2191" : L"\u2193") : value ? L"1" : L"0", -1, &label,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            draw_gate_body(memory, c.kind, pt(c.x, c.y), c.direction,
                           23, half_height(c), fill, border, selected);
        }
        for (auto const& port : ports(c)) {
            bool portValue = port.index < 0 ? values[c.id][-port.index - 1] : false;
            if (port.index >= 0)
                for (auto const& wire : circuit.wires)
                    if (wire.target == c.id && wire.port == port.index && wire_value(values, wire)) portValue = true;
            COLORREF color = portValue ? Blue : Dark;
            POINT edge{};
            if (sequential(c.kind)) {
                int x=port.index<0?60:-60, y=port.index==-1||port.index==0?-40:40;
                if(c.kind==DFlipFlop) {
                    if(port.index==1)y=20;
                    if(port.index==2){x=0;y=80;}
                    if(port.index==3){x=0;y=-80;}
                }
                edge=gate_point(pt(c.x,c.y),c.direction,x,y);
            } else if (is_source(c.kind) || c.kind == Output) {
                int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
                int sign = port.index == -1 ? -1 : 1;
                edge = pt(port.point.x + sign * 20 * dx,
                          port.point.y + sign * 20 * dy);
            } else if (port.index < 0) {
                int across = output_count(c) == 2 ? (port.index == -1 ? -Grid : Grid) : 0;
                if (sequential(c.kind)) across *= 2;
                edge = gate_point(pt(c.x, c.y), c.direction,
                                  inverted_gate(c.kind) ? 27 : 23, across);
            } else {
                int across = input_offsets(input_count(c))[port.index];
                if (sequential(c.kind)) across *= 2;
                edge = gate_point(pt(c.x, c.y), c.direction,
                                  gate_input_edge(c.kind, across, half_height(c), 23), across);
            }
            draw_line(memory, {edge, port.point}, color, false, 2);
        }
    }
    if (marquee) {
        HPEN pen = CreatePen(PS_DASH, 1, Blue);
        HGDIOBJ previousPen = SelectObject(memory, pen);
        HGDIOBJ previousBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
        Rectangle(memory, marqueeStart.x, marqueeStart.y, marqueeEnd.x, marqueeEnd.y);
        SelectObject(memory, previousBrush); SelectObject(memory, previousPen);
        DeleteObject(pen);
    }
    SetMapMode(memory, MM_TEXT);
    SetViewportOrgEx(memory, 0, 0, nullptr);
    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap); DeleteDC(memory);
}

bool circ_path(std::wstring const& path) {
    return path.size() >= 5 && _wcsicmp(path.c_str() + path.size() - 5, L".circ") == 0;
}

bool kicad_path(std::wstring const& path) {
    return path.size() >= 10 && _wcsicmp(path.c_str() + path.size() - 10, L".kicad_sch") == 0;
}

bool kicad_net_path(std::wstring const& path) {
    return path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".net") == 0;
}

bool kicad_project_path(std::wstring const& path) {
    return path.size() >= 10 && _wcsicmp(path.c_str() + path.size() - 10, L".kicad_pro") == 0;
}

std::string kicad_project_name(std::wstring const& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring stem = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    stem.resize(stem.size() - 10);
    if (stem.empty()) throw std::runtime_error("Please enter a project name before the extension.");
    int length = WideCharToMultiByte(CP_UTF8, 0, stem.data(), static_cast<int>(stem.size()), nullptr, 0, nullptr, nullptr);
    std::string name(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, stem.data(), static_cast<int>(stem.size()), &name[0], length, nullptr, nullptr);
    return name;
}

// 保存 KiCad 工程及同名逻辑原理图；已有工程的设置文件保留，只更新对应原理图。
void save_kicad_project(Circuit const& circuit, std::wstring const& destination) {
    std::string project = kicad_project_name(destination);
    std::string schematic = serialize_kicad(circuit, project);
    std::wstring schematicPath = destination.substr(0, destination.size() - 10) + L".kicad_sch";
    // 更新原理图时保留已有 KiCad 工程设置。
    if (GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string settings = "{\n  \"meta\": {\"filename\": " + kicad_quote(project + ".kicad_pro") +
            ", \"version\": 3},\n  \"schematic\": {\"meta\": {\"version\": 1}}\n}\n";
        if (!write_file(destination, settings)) throw std::runtime_error("Cannot write the KiCad project file.");
    }
    if (!write_file(schematicPath, schematic)) throw std::runtime_error("Cannot write the companion .kicad_sch file.");
}

// wxFileDialog 统一打开/另存为入口；导出后仍保留原生工程的当前路径。
std::wstring choose_file(HWND owner, bool save, DWORD& filterIndex) {
    wxString filters = save ? L"\u7B80\u6613\u7535\u8DEF JSON (*.json)|*.json|Logisim \u9879\u76EE (*.circ)|*.circ|KiCad \u5DE5\u7A0B (*.kicad_pro)|*.kicad_pro|KiCad \u539F\u7406\u56FE (*.kicad_sch)|*.kicad_sch|KiCad \u7F51\u8868 (*.net)|*.net" :
        L"\u7B80\u6613\u7535\u8DEF JSON (*.json)|*.json|Logisim \u9879\u76EE (*.circ)|*.circ|\u6240\u6709\u6587\u4EF6 (*.*)|*.*";
    wxFileDialog dialog(app.frame,save?L"\u53E6\u5B58\u4E3A / \u5BFC\u51FA":L"\u6253\u5F00",wxEmptyString,wxEmptyString,filters,
        save?wxFD_SAVE|wxFD_OVERWRITE_PROMPT:wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    dialog.SetFilterIndex(static_cast<int>(filterIndex)-1);
    if(dialog.ShowModal()!=wxID_OK)return {};
    filterIndex=dialog.GetFilterIndex()+1;
    std::wstring chosen=dialog.GetPath().ToStdWstring();
    if(save) {
        size_t slash=chosen.find_last_of(L"\\/"),dot=chosen.find_last_of(L'.');
        if(dot==std::wstring::npos || (slash!=std::wstring::npos && dot<slash)) {
            chosen+=filterIndex==5?L".net":filterIndex==4?L".kicad_sch":filterIndex==3?L".kicad_pro":filterIndex==2?L".circ":L".json";
            if(wxFileExists(chosen) && wxMessageBox(L"\u6587\u4EF6\u5DF2\u5B58\u5728\uFF0C\u662F\u5426\u8986\u76D6\uFF1F",L"\u786E\u8BA4\u8986\u76D6",wxYES_NO|wxICON_QUESTION,app.frame)!=wxYES)return {};
        }
    }
    return chosen;
}
void App::new_file() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return;
    std::wstring command = L"\"" + std::wstring(executable) + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, &command[0], nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        MessageBoxW(mainWindow, L"\u65E0\u6CD5\u6253\u5F00\u65B0\u7684\u7A7A\u767D\u7535\u8DEF\u7A97\u53E3\u3002", L"\u65B0\u5EFA\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void App::open_file() {
    DWORD filterIndex = 1;
    std::wstring chosen = choose_file(mainWindow, false, filterIndex);
    if (chosen.empty()) return;
    if (kicad_path(chosen) || kicad_project_path(chosen) || kicad_net_path(chosen)) {
        MessageBoxW(mainWindow, L"KiCad \u5DE5\u7A0B\u548C\u539F\u7406\u56FE\u8BF7\u5728 KiCad \u4E2D\u6253\u5F00\u548C\u7F16\u8F91\uFF1B\u672C\u7A0B\u5E8F\u8BF7\u6253\u5F00\u539F\u59CB JSON \u6216 .circ \u6587\u4EF6\u3002",
                    L"\u6253\u5F00 KiCad \u6587\u4EF6", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string text;
    if (!read_file(chosen, text)) {
        MessageBoxW(mainWindow, L"\u65E0\u6CD5\u8BFB\u53D6\u7535\u8DEF\u6587\u4EF6\u3002", L"\u6253\u5F00\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    try {
        Circuit loaded = circ_path(chosen) ? deserialize_circ(text) : deserialize(text);
        circuit = std::move(loaded);
        waves=WaveHistory{};
        undoHistory.clear();
    } catch (std::exception const&) {
        MessageBoxW(mainWindow, circ_path(chosen) ?
            L"\u4EC5\u80FD\u8BFB\u53D6\u672C\u7A0B\u5E8F\u4FDD\u5B58\u7684 Logisim \u9879\u76EE\uFF1B\u5176\u4ED6 .circ \u6587\u4EF6\u8BF7\u5728 Logisim \u4E2D\u6253\u5F00\u3002" :
            L"\u6587\u4EF6\u683C\u5F0F\u65E0\u6548\uFF0C\u6216\u4E0D\u5C5E\u4E8E\u7B80\u6613 Logisim \u7535\u8DEF\u3002", L"\u6253\u5F00\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    path = chosen; clear_selection(); wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); marquee = false;
    frame->SetTitle(L"\u7B80\u6613 Logisim - wxWidgets \u2014 " + path);
    update_controls(); refresh();
}

// 按扩展名分流原生保存与导出；网表提示中的数量和位号来自实际硬件清单，而不是画布符号数量。
void App::save_file(bool choosePath) {
    std::wstring destination = path;
    if (choosePath || destination.empty()) {
        DWORD filterIndex = choosePath || destination.empty() ? 3 : circ_path(destination) ? 2 : 1;
        destination = choose_file(mainWindow, true, filterIndex);
    }
    if (destination.empty()) return;
    if (kicad_path(destination) || kicad_project_path(destination) || kicad_net_path(destination)) {
        try {
            if (kicad_net_path(destination)) {
                if (!write_file(destination, serialize_kicad_net(circuit))) throw std::runtime_error("Cannot write netlist.");
                auto exported = build_pcb_circuit(circuit);
                size_t slash = destination.find_last_of(L"\\/");
                std::wstring message = L"\u5DF2\u5BFC\u51FA\u7F51\u8868\uFF1A" +
                    destination.substr(slash == std::wstring::npos ? 0 : slash + 1) +
                    L"\uFF0C\u5305\u542B " + std::to_wstring(exported.parts.size()) + L" \u4E2A\u5143\u4EF6\uFF08";
                for (size_t i = 0; i < exported.parts.size(); ++i) {
                    if (i) message += L"\u3001";
                    auto const& ref = exported.parts[i].ref;
                    message.append(ref.begin(),ref.end());
                }
                message += L"\uFF09";
                set_status(message.c_str());
                MessageBoxW(mainWindow, message.c_str(), L"KiCad .net", MB_OK | MB_ICONINFORMATION);
            } else if (kicad_project_path(destination)) {
                std::wstring schematicPath = destination.substr(0, destination.size() - 10) + L".kicad_sch";
                if (GetFileAttributesW(schematicPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
                    MessageBoxW(mainWindow, (L"\u5DE5\u7A0B\u5BF9\u5E94\u7684\u539F\u7406\u56FE\u5DF2\u5B58\u5728\uFF0C\u662F\u5426\u8986\u76D6\uFF1F\n" + schematicPath).c_str(),
                                L"\u786E\u8BA4\u8986\u76D6\u539F\u7406\u56FE", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
                save_kicad_project(circuit, destination);
                set_status(L"\u5DF2\u5BFC\u51FA KiCad \u5DE5\u7A0B\u53CA\u540C\u540D\u539F\u7406\u56FE\u3002\u8BF7\u5728 KiCad \u4E2D\u6253\u5F00 .kicad_pro\uFF0C\u518D\u53CC\u51FB\u5DE5\u7A0B\u5185\u7684 .kicad_sch\u3002");
                MessageBoxW(mainWindow, (L"KiCad \u5DE5\u7A0B\u5DF2\u4FDD\u5B58\uFF1A\n" + destination +
                    L"\n\n\u5728 KiCad \u7684\u201C\u6253\u5F00\u5DE5\u7A0B\u201D\u4E2D\u9009\u62E9\u6B64\u6587\u4EF6\uFF0C\u518D\u53CC\u51FB\u5DE5\u7A0B\u5185\u7684\u539F\u7406\u56FE\u8FDB\u884C\u7F16\u8F91\u3002\n\u8BF7\u5C06\u540C\u540D .kicad_pro \u548C .kicad_sch \u6587\u4EF6\u4FDD\u5B58\u5728\u540C\u4E00\u6587\u4EF6\u5939\u3002").c_str(),
                    L"KiCad \u5DE5\u7A0B\u5BFC\u51FA\u5B8C\u6210", MB_OK | MB_ICONINFORMATION);
            } else {
                std::string schematic = serialize_kicad(circuit, kicad_project_name(destination));
                if (!write_file(destination, schematic)) throw std::runtime_error("cannot write schematic");
                set_status(L"\u5DF2\u5BFC\u51FA\u72EC\u7ACB\u539F\u7406\u56FE\u3002\u82E5\u8981\u4ECE KiCad \u4E3B\u7A97\u53E3\u6253\u5F00\uFF0C\u8BF7\u53E6\u5B58\u4E3A KiCad \u5DE5\u7A0B (*.kicad_pro)\u3002");
            }
        } catch (std::exception const& error) {
            int length = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
            std::wstring detail(length, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, &detail[0], length);
            MessageBoxW(mainWindow, (L"\u65E0\u6CD5\u5BFC\u51FA KiCad \u6587\u4EF6\u3002\n" + detail).c_str(), L"\u5BFC\u51FA\u5931\u8D25", MB_OK | MB_ICONERROR);
        }
        return; // 导出不替换当前可编辑原生工程的保存路径。
    }
    if (!write_file(destination, circ_path(destination) ? serialize_circ(circuit) : serialize(circuit))) {
        MessageBoxW(mainWindow, L"\u65E0\u6CD5\u5199\u5165\u7535\u8DEF\u6587\u4EF6\u3002", L"\u4FDD\u5B58\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    path = destination;
    frame->SetTitle(L"\u7B80\u6613 Logisim - wxWidgets \u2014 " + path);
    set_status(L"\u5DF2\u4FDD\u5B58\u7535\u8DEF\u3002");
}

void App::handle_command(int id, int notification) {
    if (id >= ToolBase && id <= ToolBase + ConstantTool) { choose_tool(static_cast<Tool>(id - ToolBase)); return; }
    switch (id) {
    case FileNew: new_file(); break;
    case FileOpen: open_file(); break;
    case FileSave: save_file(false); break;
    case FileSaveAs: save_file(true); break;
    case FileExit: if(frame)frame->Close(); break;
    case EditUndo: undo(); break;
    case EditSelectAll: select_all(); break;
    case EditDelete: delete_selected(); break;
    case EditRotate: case RotateButton: rotate_selected(); break;
    case SimToggle: toggle_selected(); break;
    case SimReset: reset_inputs(); break;
    case CountMinus: change_count(-1); break;
    case CountPlus: change_count(1); break;
    case CountEdit: if (notification == EN_KILLFOCUS) change_count(0); break;
    case DirectionCombo: if (notification == CBN_SELCHANGE) change_direction(); break;
    case ZoomIn: zoom_center(1); break;
    case ZoomOut: zoom_center(-1); break;
    case ZoomReset: zoom_center(0); break;
    case HelpAbout:
        MessageBoxW(mainWindow,
            L"\u9009\u62E9\u5DE5\u5177\u53EF\u62D6\u52A8\u5143\u4EF6\uFF1B\u5355\u51FB\u8F93\u5165\u5F15\u811A\u5207\u6362 0 / 1\u3002\n"
            L"Ctrl+Z \u64A4\u9500\u4E0A\u4E00\u6B21\u7535\u8DEF\u4FEE\u6539\uFF1BCtrl+A \u5168\u9009\uFF1B\u4ECE\u7A7A\u767D\u5904\u62D6\u52A8\u53EF\u6846\u9009\u5143\u4EF6\u548C\u5BFC\u7EBF\u3002\n"
            L"\u9009\u4E2D\u9879\u663E\u793A\u865A\u7EBF\uFF0C\u518D\u6B21\u5355\u51FB\u5BFC\u7EBF\u6216\u70B9\u51FB\u7A7A\u767D\u5904\u53EF\u53D6\u6D88\uFF0CDelete \u5220\u9664\u3002\n\n"
            L"\u8FDE\u7EBF\u5DE5\u5177\u5148\u70B9\u8F93\u51FA\u7AEF\u6216\u5DF2\u6709\u5BFC\u7EBF\uFF0C\u518D\u70B9\u8F93\u5165\u7AEF\uFF0C\u7A7A\u767D\u5904\u53EF\u6DFB\u52A0\u6298\u70B9\u3002\n"
            L"\u5DE6\u4FA7\u5143\u4EF6\u5E93\u53EF\u9009\u62E9\u95E8\u7535\u8DEF\u3001\u534A\u52A0\u5668\u3001\u534A\u51CF\u5668\u548C\u76F8\u7B49\u6BD4\u8F83\u5668\u3002\n"
            L"\u534A\u52A0\u5668\u548C\u534A\u51CF\u5668\u7684\u4E0A\u65B9\u4E3A\u548C/\u5DEE\u4F4D\uFF0C\u4E0B\u65B9\u4E3A\u8FDB\u4F4D/\u501F\u4F4D\u3002\n"
            L"\u9009\u4E2D\u591A\u8F93\u5165\u95E8\u53EF\u8C03\u6574 2\u20138 \u4E2A\u8F93\u5165\uFF1B\u6240\u6709\u5143\u4EF6\u53EF\u65CB\u8F6C\u56DB\u4E2A\u65B9\u5411\u3002\n"
            L"\u5BFC\u7EBF\u6CBF\u7F51\u683C\u7EBF\u7ED8\u5236\uFF0C\u84DD\u8272\u8868\u793A\u771F\uFF0C\u9ED1\u8272\u8868\u793A\u5047\u3002\n"
            L"\u6EDA\u8F6E\u6216 Ctrl+\u52A0\u51CF\u53F7\u7F29\u653E\u753B\u5E03\uFF0CCtrl+0 \u590D\u539F\u3002",
            L"\u4F7F\u7528\u8BF4\u660E", MB_OK | MB_ICONINFORMATION);
        break;
    default: break;
    }
}

// wxApp 负责初始化、事件循环与退出清理，不再注册 Win32 窗口类或手写消息循环。
class LogisimApplication : public wxApp {
public:
    bool OnInit() override { app.create_ui(); SetTopWindow(app.frame); return true; }
    int OnExit() override {
        if(app.uiFont)DeleteObject(app.uiFont);
        if(app.gateFont)DeleteObject(app.gateFont);
        return wxApp::OnExit();
    }
};
} // namespace

wxIMPLEMENT_APP_NO_MAIN(LogisimApplication);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR, int show) {
    return wxEntry(instance,previous,nullptr,show);
}
int main() { return wxEntry(GetModuleHandleW(nullptr),nullptr,nullptr,SW_SHOWDEFAULT); }

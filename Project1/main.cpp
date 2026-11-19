// Self-contained Win32 application. Unicode UI text uses escapes for CP936/UTF-8 compatibility.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objbase.h>
#include <uxtheme.h>
#include <dwmapi.h>
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
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

constexpr int Grid = 20;
constexpr int SidebarWidth = 350;
constexpr int PropertyWidth = 310;
constexpr COLORREF Blue = RGB(22, 132, 232);
constexpr COLORREF Dark = RGB(39, 49, 59);
constexpr COLORREF MenuSurface = RGB(243, 243, 243);
constexpr COLORREF ToolbarSurface = RGB(243, 243, 243);
constexpr int DirectionX[4] = {1, 0, -1, 0};
constexpr int DirectionY[4] = {0, 1, 0, -1};
constexpr wchar_t const* DirectionNames[4] = {L"\u53F3", L"\u4E0B", L"\u5DE6", L"\u4E0A"};
constexpr wchar_t const* KindNames[] = {L"\u4E0E\u95E8", L"\u6216\u95E8", L"\u975E\u95E8", L"\u8F93\u5165\u5F15\u811A", L"\u8F93\u51FA\u5F15\u811A",
    L"\u5F02\u6216\u95E8", L"\u4E0E\u975E\u95E8", L"\u6216\u975E\u95E8", L"\u540C\u6216\u95E8", L"\u534A\u52A0\u5668", L"\u534A\u51CF\u5668", L"\u76F8\u7B49\u6BD4\u8F83\u5668", L"D \u89E6\u53D1\u5668", L"RS \u9501\u5B58\u5668", L"\u65F6\u949F\uFF08\u624B\u52A8\uFF09", L"\u903B\u8F91\u5E38\u91CF"};
constexpr char const* KindKeys[] = {"and", "or", "not", "input", "output",
    "xor", "nand", "nor", "xnor", "half_adder", "half_subtractor", "equal", "d_flipflop", "rs_latch", "clock", "constant"};

enum Kind { And, Or, Not, Input, Output, Xor, Nand, Nor, Xnor, HalfAdder, HalfSubtractor, Equal, DFlipFlop, RSLatch, Clock, Constant };
enum Tool { SelectTool, WireTool, AndTool, OrTool, NotTool, InputTool, OutputTool,
            XorTool, NandTool, NorTool, XnorTool, HalfAdderTool, HalfSubtractorTool, EqualTool, DFlipFlopTool, RSLatchTool, ClockTool, ConstantTool };

struct Component {
    int id = 0;
    Kind kind = And;
    int x = 0, y = 0, direction = 0, inputs = 2;
    bool value = false;
    bool lastClock = false; // Saved edge history for D flip-flops.
};

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
    int index = -1; // -1 is output; 0..7 are inputs.
    POINT point{};
};

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
int input_count(Component const& c) {
    if (c.kind == DFlipFlop) return 3;
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
    if (sequential(c.kind)) return 60;
    int result = 23;
    for (int offset : input_offsets(input_count(c))) result = std::max(result, std::abs(offset) + 12);
    return result;
}

std::vector<Port> ports(Component const& c) {
    std::vector<Port> result;
    int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
    if (is_source(c.kind)) {
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
    int hx = c.direction % 2 == 0 ? 23 : h;
    int hy = c.direction % 2 == 0 ? h : 23;
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
            if (forward <= 20 && across <= half_height(c)) return true;
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
                              hasEnd && i + 1 == destinations.size() ? target->direction : -1);
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
    // Absolute bend positions describe the old orientation; reroute both attached ends.
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

void advance_simulation(Circuit& circuit) {
    // Sample all state elements from the same settled snapshot on each event.
    for (size_t pass = 0; pass <= circuit.components.size(); ++pass) {
        auto values = signals(circuit);
        bool changed = false;
        for (auto& c : circuit.components) {
            if (!sequential(c.kind)) continue;
            bool in[3]{};
            for (auto const& w : circuit.wires)
                if (w.target == c.id && w.port >= 0 && w.port < 3) in[w.port] = wire_value(values,w);
            bool next = c.value;
            if (c.kind == RSLatch) {
                if (in[1]) next = false; // Explicit reset priority.
                else if (in[0]) next = true;
            } else {
                if (in[2]) next = false;
                else if (in[1] && !c.lastClock) next = in[0];
                c.lastClock = in[1];
            }
            changed = changed || next != c.value;
            c.value = next;
        }
        if (!changed) break;
    }
}

std::string logical_pin_name(Component const& c, int index) {
    if (sequential(c.kind)) {
        if (index < 0) return index == -1 ? "Q" : "~Q";
        if (c.kind == RSLatch) return index == 0 ? "S" : "R";
        return index == 0 ? "D" : index == 1 ? "CLK" : "R";
    }
    if (index >= 0) return "A" + std::to_string(index + 1);
    if (c.kind == HalfAdder) return index == -1 ? "S" : "C";
    if (c.kind == HalfSubtractor) return index == -1 ? "D" : "B";
    return "Y";
}

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

// KiCad schematic export is self-contained for single-file builds.
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

// Physical 5V 74HCT implementation, kept here for single-file builds.
struct PcbPart {
    std::string ref, value, footprint;
    std::map<int, std::string> pins;
};
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
    int chip(std::string value) {
        int index = add("U", value, "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm", {{7,"GND"},{14,"+5V"}});
        add("C", "100n", "Capacitor_SMD:C_0805_2012Metric", {{1,"+5V"},{2,"GND"}});
        return index;
    }
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
    std::string xor_gate(std::string a, std::string b) {
        auto t = nand_gate(a,b), x = nand_gate(a,t), y = nand_gate(b,t);
        return nand_gate(x,y);
    }
    std::string schmitt(std::string a) {
        static int const pins[6][2] = {{1,2},{3,4},{5,6},{9,8},{11,10},{13,12}};
        if (invSlot == 6) { invChip = chip("74HCT14D"); invSlot = 0; }
        auto const& p = pins[invSlot++];
        std::string net = "N" + std::to_string(++nextNet);
        parts[invChip].pins[p[0]] = a; parts[invChip].pins[p[1]] = net;
        return net;
    }
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
                    // First half used; second half held in reset with all inputs defined.
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
                auto& p = pcb.parts[flipChips.at(c.id)];
                p.pins[1] = clear; p.pins[2] = ins[0]; p.pins[3] = ins[1];
                result = outputs.at({c.id,-1});
            } else if (c.kind == RSLatch) {
                auto nr = pcb.invert(ins[1]);
                auto ns = pcb.nand_gate(ins[0],nr); // S is inhibited while R is high.
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


std::string serialize_kicad(Circuit const& circuit, std::string const& project) {
    constexpr double scale = 0.127; // One 20-pixel grid step is 100 mil (2.54 mm).
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
        // Remove grid-step vertices: an unrelated crossing must not become a junction.
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
    // KiCad connects touching endpoints and overlapping wires. Use named nets for
    // conflicting routes rather than changing the source circuit's connectivity.
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
    // A same-net junction on a foreign crossing would short the foreign net too.
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
            << "        (rectangle (start -2.54 " << h << ") (end 2.54 " << -h
            << ") (stroke (width 0.254) (type default)) (fill (type background)))\n"
            << "        (text \"" << captions[c.kind] << "\" (at 0 0 0) (effects (font (size 1.016 1.016))))\n      )\n"
            << "      (symbol \"" << symbol << "_1_1\"\n";
        auto localPins = ports(c);
        for (size_t i = 0; i < localPins.size(); ++i) {
            auto const& p = localPins[i];
            bool output = p.index < 0;
            out << "        (pin " << (output ? "output" : "input") << ' '
                << (output && inverted_gate(c.kind) ? "inverted" : "line")
                << " (at " << p.point.x * scale << ' ' << -p.point.y * scale << ' ' << (output ? 180 : 0)
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

class App {
public:
    struct UndoState {
        Circuit circuit;
        std::set<int> selectedComponents, selectedWires;
    };
    HWND mainWindow = nullptr, canvas = nullptr, status = nullptr, selectionLabel = nullptr;
    HWND libraryTitle = nullptr, libraryTree = nullptr;
    HWND propertyTitle = nullptr, inputCountLabel = nullptr, directionLabel = nullptr;
    HWND tipsTitle = nullptr, tipsText = nullptr;
    HWND countEdit = nullptr, countMinus = nullptr, countPlus = nullptr;
    HWND directionCombo = nullptr, rotateButton = nullptr;
    HWND toolButtons[7]{}, tooltips = nullptr;
    HFONT uiFont = nullptr, gateFont = nullptr;
    HBRUSH menuBrush = nullptr, toolbarBrush = nullptr;
    Circuit circuit;
    std::vector<UndoState> undoHistory;
    std::wstring path;
    std::wstring menuTipText, menuPreviousStatus;
    Tool tool = SelectTool;
    std::set<int> selectedComponents, selectedWires;
    bool updatingControls = false;
    bool draggingComponent = false, dragged = false, marquee = false, dragUndoRecorded = false;
    POINT press{}, dragOffset{}, dragStartPosition{}, marqueeStart{}, marqueeEnd{}, hover{};
    bool hoverValid = false;
    bool menuHintActive = false;
    int dragComponentId = 0, wireStartId = 0, wireStartPort = -1, branchFromWireId = 0;
    int zoomPercent = 100, viewX = 0, viewY = 0, wheelRemainder = 0;
    POINT branchPoint{};
    std::vector<POINT> bends;

    void create_ui(HINSTANCE instance);
    void layout();
    POINT screen_to_world(POINT point) const;
    void zoom_at(int direction, POINT anchor);
    void zoom_center(int direction);
    bool wheel_zoom(POINT screen, int delta);
    void refresh() { advance_simulation(circuit); if (canvas) InvalidateRect(canvas, nullptr, FALSE); }
    void set_status(wchar_t const* message) { if (status) SetWindowTextW(status, message); }
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
    void show_menu_hint(HMENU menu, UINT item, UINT flags);
};

App app;
constexpr wchar_t MainClass[] = L"MiniLogisimMainCpp";
constexpr wchar_t CanvasClass[] = L"MiniLogisimCanvasCpp";

HWND make_control(wchar_t const* className, wchar_t const* label, DWORD style, int id, HWND parent) {
    HWND control = CreateWindowExW(0, className, label, WS_CHILD | WS_VISIBLE | style,
                                   0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE);
    return control;
}

struct MenuLabel {
    wchar_t const* text;
    wchar_t const* shortcut;
    bool onBar;
    wchar_t const* description;
};

HMENU make_menu() {
    static MenuLabel const labels[] = {
        {L"\u6587\u4EF6", L"", true, L"\u65B0\u5EFA\u3001\u6253\u5F00\u548C\u4FDD\u5B58\u7535\u8DEF"},
        {L"\u7F16\u8F91", L"", true, L"\u64A4\u9500\u3001\u5168\u9009\u3001\u5220\u9664\u6216\u65CB\u8F6C\u9009\u4E2D\u5BF9\u8C61"},
        {L"\u9879\u76EE", L"", true, L"\u9009\u62E9\u95E8\u7535\u8DEF\u6216\u8F93\u5165\u3001\u8F93\u51FA\u5F15\u811A"},
        {L"\u6A21\u62DF", L"", true, L"\u5207\u6362\u6216\u91CD\u7F6E\u8F93\u5165\u4FE1\u53F7"},
        {L"\u5E2E\u52A9", L"", true, L"\u67E5\u770B\u7A97\u53E3\u64CD\u4F5C\u8BF4\u660E"},
        {L"\u65B0\u5EFA\u7A97\u53E3", L"Ctrl+N", false, L"\u6253\u5F00\u65B0\u7684\u7A7A\u767D\u7535\u8DEF\u7A97\u53E3"},
        {L"\u6253\u5F00\u2026", L"Ctrl+O", false, L"\u4ECE JSON \u6216\u672C\u7A0B\u5E8F\u4FDD\u5B58\u7684 .circ \u6587\u4EF6\u6253\u5F00\u7535\u8DEF"},
        {L"\u4FDD\u5B58", L"Ctrl+S", false, L"\u5C06\u5F53\u524D\u7535\u8DEF\u4FDD\u5B58\u5230\u6587\u4EF6"},
        {L"\u53E6\u5B58\u4E3A\u2026", L"Ctrl+Shift+S", false, L"\u9009\u62E9\u65B0\u6587\u4EF6\u4FDD\u5B58\u7535\u8DEF"},
        {L"\u9000\u51FA", L"Ctrl+Q", false, L"\u5173\u95ED\u5F53\u524D\u7A97\u53E3"},
        {L"\u5168\u9009", L"Ctrl+A", false, L"\u9009\u4E2D\u753B\u5E03\u4E0A\u5168\u90E8\u5143\u4EF6\u548C\u5BFC\u7EBF"},
        {L"\u5220\u9664\u9009\u4E2D\u5BF9\u8C61", L"Delete", false, L"\u5220\u9664\u9009\u4E2D\u5143\u4EF6\u548C\u5BFC\u7EBF"},
        {L"\u987A\u65F6\u9488\u65CB\u8F6C", L"R", false, L"\u5C06\u9009\u4E2D\u5143\u4EF6\u987A\u65F6\u9488\u65CB\u8F6C 90 \u5EA6"},
        {L"\u4E0E\u95E8", L"", false, L"\u653E\u7F6E\u4E0E\u95E8\uFF1A\u6240\u6709\u8F93\u5165\u4E3A\u771F\u65F6\u8F93\u51FA\u4E3A\u771F"},
        {L"\u6216\u95E8", L"", false, L"\u653E\u7F6E\u6216\u95E8\uFF1A\u4EFB\u4E00\u8F93\u5165\u4E3A\u771F\u65F6\u8F93\u51FA\u4E3A\u771F"},
        {L"\u975E\u95E8", L"", false, L"\u653E\u7F6E\u975E\u95E8\uFF1A\u8F93\u51FA\u4E3A\u8F93\u5165\u7684\u76F8\u53CD\u503C"},
        {L"\u8F93\u5165\u5F15\u811A", L"", false, L"\u653E\u7F6E\u53EF\u5355\u51FB\u5207\u6362 0/1 \u7684\u8F93\u5165\u5F15\u811A"},
        {L"\u8F93\u51FA\u5F15\u811A", L"", false, L"\u653E\u7F6E\u663E\u793A\u7535\u8DEF\u7ED3\u679C\u7684\u8F93\u51FA\u5F15\u811A"},
        {L"\u5207\u6362\u9009\u4E2D\u8F93\u5165\u5F15\u811A", L"", false, L"\u5207\u6362\u9009\u4E2D\u8F93\u5165\u5F15\u811A\u7684 0/1 \u72B6\u6001"},
        {L"\u6240\u6709\u8F93\u5165\u8BBE\u4E3A 0", L"", false, L"\u91CD\u7F6E\u5168\u90E8\u8F93\u5165\u5F15\u811A\u4E3A 0"},
        {L"\u4F7F\u7528\u8BF4\u660E", L"", false, L"\u67E5\u770B\u9009\u62E9\u3001\u8FDE\u7EBF\u548C\u6A21\u62DF\u7684\u64CD\u4F5C\u8BF4\u660E"},
        {L"\u64A4\u9500", L"Ctrl+Z", false, L"\u8FD8\u539F\u4E0A\u4E00\u6B21\u7535\u8DEF\u4FEE\u6539"}
    };
    auto add = [&](HMENU menu, UINT flags, UINT_PTR id, int label) {
        AppendMenuW(menu, flags | MF_OWNERDRAW, id,
                    reinterpret_cast<LPCWSTR>(&labels[label]));
    };
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    add(file, 0, FileNew, 5);
    add(file, 0, FileOpen, 6);
    add(file, 0, FileSave, 7);
    add(file, 0, FileSaveAs, 8);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    add(file, 0, FileExit, 9);
    add(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), 0);
    HMENU edit = CreatePopupMenu();
    add(edit, 0, EditUndo, 21);
    add(edit, 0, EditSelectAll, 10);
    add(edit, 0, EditDelete, 11);
    add(edit, 0, EditRotate, 12);
    add(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), 1);
    HMENU project = CreatePopupMenu();
    for (int i = AndTool; i <= OutputTool; ++i)
        add(project, 0, ToolBase + i, 13 + i - AndTool);
    add(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(project), 2);
    HMENU simulation = CreatePopupMenu();
    add(simulation, 0, SimToggle, 18);
    add(simulation, 0, SimReset, 19);
    add(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(simulation), 3);
    HMENU help = CreatePopupMenu();
    add(help, 0, HelpAbout, 20);
    add(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), 4);
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = app.menuBrush;
    SetMenuInfo(bar, &info);
    return bar;
}

void App::create_ui(HINSTANCE instance) {
    menuBrush = CreateSolidBrush(MenuSurface);
    toolbarBrush = CreateSolidBrush(ToolbarSurface);
    uiFont = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei UI");
    gateFont = CreateFontW(-31, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI Symbol");
    SetMenu(mainWindow, make_menu());
    DWORD rounded = 2; DwmSetWindowAttribute(mainWindow, 33, &rounded, sizeof(rounded));
    wchar_t const* labels[7] = {L"\u9009\u62E9", L"\u8FDE\u7EBF", L"\u4E0E\u95E8", L"\u6216\u95E8", L"\u975E\u95E8", L"\u8F93\u5165\u5F15\u811A", L"\u8F93\u51FA\u5F15\u811A"};
    tooltips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                mainWindow, nullptr, instance, nullptr);
    SendMessageW(tooltips, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    SendMessageW(tooltips, TTM_SETMAXTIPWIDTH, 0, 420);
    auto add_tooltip = [&](HWND control, wchar_t const* description) {
        TOOLINFOW tip{};
        tip.cbSize = TTTOOLINFOW_V2_SIZE;
        tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tip.hwnd = mainWindow;
        tip.uId = reinterpret_cast<UINT_PTR>(control);
        tip.lpszText = const_cast<LPWSTR>(description);
        SendMessageW(tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
    };
    TOOLINFOW menuTip{};
    menuTip.cbSize = TTTOOLINFOW_V2_SIZE;
    menuTip.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    menuTip.hwnd = mainWindow;
    menuTip.uId = 1;
    menuTip.lpszText = const_cast<LPWSTR>(L"");
    SendMessageW(tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&menuTip));
    wchar_t const* descriptions[7] = {
        L"\u9009\u62E9\uFF1A\u79FB\u52A8\u5143\u4EF6\u3001\u9009\u4E2D\u5BFC\u7EBF\uFF0C\u6216\u4ECE\u7A7A\u767D\u5904\u62D6\u52A8\u6846\u9009",
        L"\u8FDE\u7EBF\uFF1A\u4ECE\u8F93\u51FA\u7AEF\u6216\u5DF2\u6709\u5BFC\u7EBF\u5F00\u59CB\uFF0C\u5355\u51FB\u8F93\u5165\u7AEF\u5B8C\u6210\u8FDE\u63A5",
        L"\u4E0E\u95E8\uFF1A\u653E\u7F6E\u53EF\u8C03\u6574\u8F93\u5165\u6570\u91CF\u7684\u4E0E\u95E8",
        L"\u6216\u95E8\uFF1A\u653E\u7F6E\u53EF\u8C03\u6574\u8F93\u5165\u6570\u91CF\u7684\u6216\u95E8",
        L"\u975E\u95E8\uFF1A\u653E\u7F6E\u5C06\u8F93\u5165\u53D6\u53CD\u7684\u975E\u95E8",
        L"\u8F93\u5165\u5F15\u811A\uFF1A\u653E\u7F6E\u540E\u5355\u51FB\u53EF\u5207\u6362 0/1",
        L"\u8F93\u51FA\u5F15\u811A\uFF1A\u663E\u793A\u8FDE\u63A5\u5904\u7684\u7535\u8DEF\u7ED3\u679C"
    };
    for (int i = 0; i < 7; ++i) {
        toolButtons[i] = make_control(L"BUTTON", labels[i], BS_OWNERDRAW,
                                      ToolBase + i, mainWindow);
        add_tooltip(toolButtons[i], descriptions[i]);
    }
    libraryTitle = make_control(L"STATIC", L"\u5143\u4EF6\u5E93", SS_LEFT, 0, mainWindow);
    libraryTree = make_control(WC_TREEVIEWW, L"", WS_TABSTOP |
                               TVS_HASBUTTONS | TVS_LINESATROOT |
                               TVS_SHOWSELALWAYS | TVS_INFOTIP, LibraryTree, mainWindow);
    SendMessageW(libraryTree, TVM_SETITEMHEIGHT, 34, 0);
    auto insert_library_item = [&](HTREEITEM parent, wchar_t const* text, int toolIndex) {
        TVINSERTSTRUCTW item{};
        item.hParent = parent;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = const_cast<LPWSTR>(text);
        item.item.lParam = toolIndex + 1;
        return TreeView_InsertItem(libraryTree, &item);
    };
    HTREEITEM gates = insert_library_item(TVI_ROOT, L"\u95E8\u7535\u8DEF", -1);
    for (int index : {AndTool, OrTool, NotTool, XorTool, NandTool, NorTool, XnorTool})
        insert_library_item(gates, KindNames[index - AndTool], index);
    HTREEITEM arithmetic = insert_library_item(TVI_ROOT, L"\u8FD0\u7B97\u5668", -1);
    for (int index : {HalfAdderTool, HalfSubtractorTool, EqualTool})
        insert_library_item(arithmetic, KindNames[index - AndTool], index);
    HTREEITEM pins = insert_library_item(TVI_ROOT, L"\u5F15\u811A", -1);
    for (int index : {InputTool, OutputTool})
        insert_library_item(pins, KindNames[index - AndTool], index);
    HTREEITEM storage = insert_library_item(TVI_ROOT, L"\u89E6\u53D1\u5668", -1);
    for (int index : {DFlipFlopTool, RSLatchTool}) insert_library_item(storage, KindNames[index - AndTool], index);
    for (int index : {ClockTool, ConstantTool}) insert_library_item(pins, KindNames[index - AndTool], index);
    SetWindowTheme(libraryTree, L"Explorer", nullptr);
    TreeView_SetBkColor(libraryTree, RGB(255,255,255));
    TreeView_SetTextColor(libraryTree, Dark);
    TreeView_Expand(libraryTree, gates, TVE_EXPAND);
    propertyTitle = make_control(L"STATIC", L"\u5C5E\u6027", SS_LEFT, 0, mainWindow);
    selectionLabel = make_control(L"STATIC", L"\u672A\u9009\u4E2D\u5143\u4EF6", SS_LEFT, 0, mainWindow);
    inputCountLabel = make_control(L"STATIC", L"\u8F93\u5165\u6570", SS_LEFT, 0, mainWindow);
    countMinus = make_control(L"BUTTON", L"\u2212", BS_PUSHBUTTON, CountMinus, mainWindow);
    countEdit = make_control(L"EDIT", L"2", WS_BORDER | ES_CENTER | ES_NUMBER, CountEdit, mainWindow);
    countPlus = make_control(L"BUTTON", L"+", BS_PUSHBUTTON, CountPlus, mainWindow);
    directionLabel = make_control(L"STATIC", L"\u671D\u5411", SS_LEFT, 0, mainWindow);
    directionCombo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                   DirectionCombo, mainWindow);
    for (auto name : DirectionNames) SendMessageW(directionCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    rotateButton = make_control(L"BUTTON", L"\u987A\u65F6\u9488\u65CB\u8F6C  R", BS_PUSHBUTTON, RotateButton, mainWindow);
    tipsTitle = make_control(L"STATIC", L"\u64CD\u4F5C\u63D0\u793A", SS_LEFT, 0, mainWindow);
    tipsText = make_control(L"STATIC", L"Ctrl+Z \u64A4\u9500  Ctrl+A \u5168\u9009\r\nDelete \u5220\u9664\r\n\u62D6\u52A8\u6846\u9009  \u8F93\u5165\u5207\u6362\r\n\u5BFC\u7EBF\u5206\u652F  \u7A7A\u767D\u6298\u70B9\r\n\u6EDA\u8F6E\u7F29\u653E  Ctrl+0 \u590D\u539F",
                            SS_LEFT, 0, mainWindow);
    status = make_control(L"STATIC", L"\u9009\u62E9\u5DE5\u5177\uFF1B\u5355\u51FB\u8F93\u5165\u5F15\u811A\u53EF\u5207\u6362 0 / 1\u3002",
                          SS_LEFT | SS_SUNKEN, 0, mainWindow);
    add_tooltip(countMinus, L"\u51CF\u5C11\u9009\u4E2D\u95E8\u7535\u8DEF\u7684\u8F93\u5165\u5F15\u811A\u6570\u91CF");
    add_tooltip(countEdit, L"\u8F93\u5165 2 \u81F3 8\uFF0C\u8BBE\u7F6E\u9009\u4E2D\u95E8\u7684\u8F93\u5165\u5F15\u811A\u6570\u91CF");
    add_tooltip(countPlus, L"\u589E\u52A0\u9009\u4E2D\u95E8\u7535\u8DEF\u7684\u8F93\u5165\u5F15\u811A\u6570\u91CF");
    add_tooltip(directionCombo, L"\u8BBE\u7F6E\u9009\u4E2D\u95E8\u6216\u5F15\u811A\u7684\u671D\u5411");
    add_tooltip(rotateButton, L"\u5C06\u9009\u4E2D\u95E8\u6216\u5F15\u811A\u987A\u65F6\u9488\u65CB\u8F6C 90 \u5EA6");
    canvas = CreateWindowExW(0, CanvasClass, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 100, 100, mainWindow, nullptr, instance, nullptr);
    layout();
    update_controls();
    InvalidateRect(toolButtons[0], nullptr, TRUE);
}

void App::layout() {
    if (!canvas) return;
    RECT client{}; GetClientRect(mainWindow, &client);
    int width = client.right, height = client.bottom;
    for (int i = 0; i < 7; ++i) MoveWindow(toolButtons[i], 14 + i * 106, 10, 96, 54, TRUE);
    int right = std::max(SidebarWidth + 120, width - PropertyWidth);
    MoveWindow(canvas, SidebarWidth, 80, std::max(1, right - SidebarWidth - 12), std::max(1, height - 122), TRUE);
    MoveWindow(status, 12, height - 38, width - 24, 34, TRUE);
    MoveWindow(libraryTitle, 20, 90, SidebarWidth - 40, 34, TRUE);
    MoveWindow(libraryTree, 14, 130, SidebarWidth - 28, std::max(100,height - 184), TRUE);
    MoveWindow(propertyTitle, right + 16, 90, PropertyWidth - 32, 34, TRUE);
    MoveWindow(selectionLabel, right + 16, 130, PropertyWidth - 32, 38, TRUE);
    MoveWindow(inputCountLabel, right + 16, 183, 85, 34, TRUE);
    MoveWindow(countMinus, right + 105, 178, 36, 40, TRUE);
    MoveWindow(countEdit, right + 145, 178, 54, 40, TRUE);
    MoveWindow(countPlus, right + 203, 178, 36, 40, TRUE);
    MoveWindow(directionLabel, right + 16, 235, 64, 34, TRUE);
    MoveWindow(directionCombo, right + 86, 230, PropertyWidth - 108, 170, TRUE);
    MoveWindow(rotateButton, right + 16, 286, PropertyWidth - 32, 44, TRUE);
    MoveWindow(tipsTitle, right + 16, 350, PropertyWidth - 32, 34, TRUE);
    MoveWindow(tipsText, right + 16, 394, PropertyWidth - 32, std::max(50,height - 450), TRUE);
}

POINT App::screen_to_world(POINT point) const {
    return pt(static_cast<int>(std::lround((point.x - viewX) * 100.0 / zoomPercent)),
              static_cast<int>(std::lround((point.y - viewY) * 100.0 / zoomPercent)));
}

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

void App::update_controls() {
    if (!selectionLabel) return;
    int count = static_cast<int>(selectedComponents.size() + selectedWires.size());
    int single = single_component();
    Component const* component = find_component(circuit, single);
    std::wstring label = count > 1 ? L"\u5DF2\u9009\u4E2D " + std::to_wstring(count) + L" \u9879" :
                         component ? KindNames[component->kind] : count == 1 ? L"\u5BFC\u7EBF" : L"\u672A\u9009\u4E2D\u5143\u4EF6";
    SetWindowTextW(selectionLabel, label.c_str());
    bool gate = component && adjustable_gate(component->kind);
    EnableWindow(countEdit, gate); EnableWindow(countMinus, gate); EnableWindow(countPlus, gate);
    EnableWindow(directionCombo, component != nullptr); EnableWindow(rotateButton, component != nullptr);
    if (component) {
        updatingControls = true;
        SetWindowTextW(countEdit, std::to_wstring(component->inputs).c_str());
        SendMessageW(directionCombo, CB_SETCURSEL, component->direction, 0);
        updatingControls = false;
    }
}

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

void App::show_menu_hint(HMENU menu, UINT item, UINT flags) {
    TOOLINFOW tip{};
    tip.cbSize = TTTOOLINFOW_V2_SIZE;
    tip.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    tip.hwnd = mainWindow;
    tip.uId = 1;
    if (!menu || item == 0xFFFF || (flags & MF_SEPARATOR)) {
        SendMessageW(tooltips, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tip));
        if (menuHintActive) set_status(menuPreviousStatus.c_str());
        menuHintActive = false;
        return;
    }
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_DATA;
    if (!GetMenuItemInfoW(menu, item, (flags & MF_POPUP) != 0, &info)) return;
    auto const* label = reinterpret_cast<MenuLabel const*>(info.dwItemData);
    if (!label) return;
    if (!menuHintActive) {
        wchar_t previous[256]{};
        GetWindowTextW(status, previous, 256);
        menuPreviousStatus = previous;
        menuHintActive = true;
    }
    menuTipText = label->description;
    tip.lpszText = const_cast<LPWSTR>(menuTipText.c_str());
    SendMessageW(tooltips, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tip));
    POINT cursor{};
    GetCursorPos(&cursor);
    SendMessageW(tooltips, TTM_TRACKPOSITION, 0,
                 MAKELPARAM(std::max<LONG>(0, std::min<LONG>(cursor.x + 300, GetSystemMetrics(SM_CXSCREEN) - 420)),
                            cursor.y + (label->onBar ? 100 : 12)));
    SendMessageW(tooltips, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tip));
    set_status(label->description);
}

void App::choose_tool(Tool next) {
    tool = next;
    if (libraryTree) {
        HTREEITEM selected = TreeView_GetSelection(libraryTree);
        TVITEMW item{};
        item.mask = TVIF_PARAM;
        item.hItem = selected;
        if (selected && TreeView_GetItem(libraryTree, &item) && item.lParam != next + 1)
            TreeView_SelectItem(libraryTree, nullptr);
    }
    wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); hoverValid = false;
    marquee = draggingComponent = false;
    if (next != SelectTool) clear_selection();
    for (auto button : toolButtons) InvalidateRect(button, nullptr, TRUE);
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
    int direction = static_cast<int>(SendMessageW(directionCombo, CB_GETCURSEL, 0, 0));
    if (direction >= 0 && direction < 4 && direction != component->direction) {
        remember_change();
        set_component_direction(circuit, component->id, direction);
    }
    refresh();
}

void App::toggle_selected() {
    Component* component = find_component(circuit, single_component());
    if (component && is_source(component->kind)) {
        remember_change();
        component->value = !component->value; refresh();
    }
}

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

void draw_shape(HDC dc, RECT bounds, bool ellipse, COLORREF fill, COLORREF border, bool dashed) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(dashed ? PS_DASH : PS_SOLID, dashed ? 1 : 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    if (ellipse) Ellipse(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
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

void draw_toolbar_button(DRAWITEMSTRUCT const& item) {
    int index = static_cast<int>(item.CtlID) - ToolBase;
    if (index < 0 || index >= 7) return;
    HDC dc = item.hDC;
    RECT bounds = item.rcItem;
    bool selected = app.tool == index;
    HBRUSH background = CreateSolidBrush(selected ? RGB(220, 237, 255) :
                                          (item.itemState & ODS_SELECTED) ? RGB(231, 236, 241) : RGB(250, 251, 253));
    FillRect(dc, &bounds, background);
    DeleteObject(background);
    HPEN border = CreatePen(PS_SOLID, selected ? 2 : 1,
                            selected ? Blue : RGB(170, 181, 191));
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, 12, 12);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(border);
    int cx = (bounds.left + bounds.right) / 2;
    int cy = (bounds.top + bounds.bottom) / 2;
    if (index == SelectTool) {
        POINT arrow[] = {pt(cx - 15, cy - 20), pt(cx - 13, cy + 16), pt(cx - 5, cy + 7),
                         pt(cx + 2, cy + 20), pt(cx + 9, cy + 16), pt(cx + 2, cy + 4),
                         pt(cx + 15, cy + 2)};
        HBRUSH brush = CreateSolidBrush(Dark);
        HPEN pen = CreatePen(PS_SOLID, 1, Dark);
        oldBrush = SelectObject(dc, brush); oldPen = SelectObject(dc, pen);
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
    if (item.itemState & ODS_FOCUS) {
        RECT focus = bounds;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(dc, &focus);
    }
}

void measure_menu_item(MEASUREITEMSTRUCT& item) {
    auto const* label = reinterpret_cast<MenuLabel const*>(item.itemData);
    if (!label) return;
    HDC dc = GetDC(app.mainWindow);
    HGDIOBJ oldFont = SelectObject(dc, app.uiFont);
    SIZE text{}, shortcut{};
    GetTextExtentPoint32W(dc, label->text, lstrlenW(label->text), &text);
    GetTextExtentPoint32W(dc, label->shortcut, lstrlenW(label->shortcut), &shortcut);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    item.itemWidth = text.cx + (label->onBar ? 28 : 42 + (shortcut.cx ? shortcut.cx + 28 : 0));
    item.itemHeight = metrics.tmHeight + 12;
    SelectObject(dc, oldFont);
    ReleaseDC(app.mainWindow, dc);
}

void draw_menu_item(DRAWITEMSTRUCT const& item) {
    auto const* label = reinterpret_cast<MenuLabel const*>(item.itemData);
    if (!label) return;
    bool selected = (item.itemState & ODS_SELECTED) != 0;
    FillRect(item.hDC, &item.rcItem,
             selected ? GetSysColorBrush(COLOR_HIGHLIGHT) :
             label->onBar ? app.menuBrush : GetSysColorBrush(COLOR_MENU));
    HGDIOBJ oldFont = SelectObject(item.hDC, app.uiFont);
    int oldMode = SetBkMode(item.hDC, TRANSPARENT);
    COLORREF oldColor = SetTextColor(item.hDC,
        GetSysColor(item.itemState & ODS_DISABLED ? COLOR_GRAYTEXT :
                    selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    RECT textRect = item.rcItem;
    textRect.left += label->onBar ? 14 : 20;
    textRect.right -= label->onBar ? 14 : 16;
    if (*label->shortcut) {
        SIZE size{};
        GetTextExtentPoint32W(item.hDC, label->shortcut, lstrlenW(label->shortcut), &size);
        RECT shortcutRect = textRect;
        shortcutRect.left = shortcutRect.right - size.cx;
        DrawTextW(item.hDC, label->shortcut, -1, &shortcutRect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        textRect.right = shortcutRect.left - 20;
    }
    DrawTextW(item.hDC, label->text, -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SetTextColor(item.hDC, oldColor);
    SetBkMode(item.hDC, oldMode);
    SelectObject(item.hDC, oldFont);
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
            if (is_source(c.kind) || c.kind == Output) {
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
            if (sequential(c.kind)) {
                auto name = logical_pin_name(c,port.index); std::wstring text(name.begin(),name.end());
                RECT r{port.point.x-28,port.point.y-30,port.point.x+40,port.point.y-4};
                SelectObject(memory,uiFont); SetTextColor(memory,Dark); DrawTextW(memory,text.c_str(),-1,&r,DT_SINGLELINE | DT_LEFT);
            }
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

void save_kicad_project(Circuit const& circuit, std::wstring const& destination) {
    std::string project = kicad_project_name(destination);
    std::string schematic = serialize_kicad(circuit, project);
    std::wstring schematicPath = destination.substr(0, destination.size() - 10) + L".kicad_sch";
    // Keep settings from an existing KiCad project when updating its schematic.
    if (GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string settings = "{\n  \"meta\": {\"filename\": " + kicad_quote(project + ".kicad_pro") +
            ", \"version\": 3},\n  \"schematic\": {\"meta\": {\"version\": 1}}\n}\n";
        if (!write_file(destination, settings)) throw std::runtime_error("Cannot write the KiCad project file.");
    }
    if (!write_file(schematicPath, schematic)) throw std::runtime_error("Cannot write the companion .kicad_sch file.");
}

std::wstring choose_file(HWND owner, bool save, DWORD& filterIndex) {
    wchar_t buffer[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = save ?
        L"\u7B80\u6613\u7535\u8DEF JSON (*.json)\0*.json\0Logisim \u9879\u76EE (*.circ)\0*.circ\0KiCad \u5DE5\u7A0B (*.kicad_pro)\0*.kicad_pro\0KiCad \u539F\u7406\u56FE (*.kicad_sch)\0*.kicad_sch\0KiCad \u7F51\u8868 (*.net)\0*.net\0\0" :
        L"\u7B80\u6613\u7535\u8DEF JSON (*.json)\0*.json\0Logisim \u9879\u76EE (*.circ)\0*.circ\0\u6240\u6709\u6587\u4EF6 (*.*)\0*.*\0\0";
    dialog.nFilterIndex = filterIndex;
    dialog.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
    filterIndex = dialog.nFilterIndex;
    std::wstring chosen = buffer;
    if (save) {
        size_t slash = chosen.find_last_of(L"\\/");
        size_t dot = chosen.find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
            chosen += filterIndex == 5 ? L".net" : filterIndex == 3 ? L".kicad_pro" : filterIndex == 4 ? L".kicad_sch" : filterIndex == 2 ? L".circ" : L".json";
            if (GetFileAttributesW(chosen.c_str()) != INVALID_FILE_ATTRIBUTES &&
                MessageBoxW(owner, L"\u6587\u4EF6\u5DF2\u5B58\u5728\uFF0C\u662F\u5426\u8986\u76D6\uFF1F", L"\u786E\u8BA4\u8986\u76D6", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return {};
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
        undoHistory.clear();
    } catch (std::exception const&) {
        MessageBoxW(mainWindow, circ_path(chosen) ?
            L"\u4EC5\u80FD\u8BFB\u53D6\u672C\u7A0B\u5E8F\u4FDD\u5B58\u7684 Logisim \u9879\u76EE\uFF1B\u5176\u4ED6 .circ \u6587\u4EF6\u8BF7\u5728 Logisim \u4E2D\u6253\u5F00\u3002" :
            L"\u6587\u4EF6\u683C\u5F0F\u65E0\u6548\uFF0C\u6216\u4E0D\u5C5E\u4E8E\u7B80\u6613 Logisim \u7535\u8DEF\u3002", L"\u6253\u5F00\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    path = chosen; clear_selection(); wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); marquee = false;
    SetWindowTextW(mainWindow, (L"\u7B80\u6613 Logisim C++ \u2014 " + path).c_str());
    update_controls(); refresh();
}

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
        return; // Export does not replace the editable native document path.
    }
    if (!write_file(destination, circ_path(destination) ? serialize_circ(circuit) : serialize(circuit))) {
        MessageBoxW(mainWindow, L"\u65E0\u6CD5\u5199\u5165\u7535\u8DEF\u6587\u4EF6\u3002", L"\u4FDD\u5B58\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }
    path = destination;
    SetWindowTextW(mainWindow, (L"\u7B80\u6613 Logisim C++ \u2014 " + path).c_str());
    set_status(L"\u5DF2\u4FDD\u5B58\u7535\u8DEF\u3002");
}

void App::handle_command(int id, int notification) {
    if (id >= ToolBase && id < ToolBase + 7) { choose_tool(static_cast<Tool>(id - ToolBase)); return; }
    switch (id) {
    case FileNew: new_file(); break;
    case FileOpen: open_file(); break;
    case FileSave: save_file(false); break;
    case FileSaveAs: save_file(true); break;
    case FileExit: DestroyWindow(mainWindow); break;
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

LRESULT CALLBACK CanvasProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
    case WM_LBUTTONDOWN: app.canvas_down(app.screen_to_world(point)); return 0;
    case WM_MOUSEMOVE: app.canvas_move(app.screen_to_world(point), (wParam & MK_LBUTTON) != 0); return 0;
    case WM_LBUTTONUP: app.canvas_up(app.screen_to_world(point)); return 0;
    case WM_MOUSEWHEEL:
        if (app.wheel_zoom(point, GET_WHEEL_DELTA_WPARAM(wParam))) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, app.tool == SelectTool ? IDC_ARROW : IDC_CROSS)); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        app.paint_canvas(dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK MainProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        app.mainWindow = window;
        app.create_ui(reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance);
        return 0;
    case WM_GETMINMAXINFO: { auto* info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = POINT{1000,650}; return 0; }
    case WM_CTLCOLORSTATIC: SetBkColor(reinterpret_cast<HDC>(wParam), ToolbarSurface); SetTextColor(reinterpret_cast<HDC>(wParam), Dark); return reinterpret_cast<LRESULT>(app.toolbarBrush);
    case WM_SIZE: app.layout(); return 0;
    case WM_NCMOUSEMOVE:
        if (wParam == HTMENU) {
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HMENU menu = GetMenu(window);
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                RECT bounds{};
                if (GetMenuItemRect(window, menu, i, &bounds) && PtInRect(&bounds, cursor)) {
                    app.show_menu_hint(menu, i, MF_POPUP);
                    break;
                }
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_NONCLIENT, window, 0};
            TrackMouseEvent(&tracking);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_NCMOUSELEAVE:
        app.show_menu_hint(nullptr, 0xFFFF, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{}; GetClientRect(window, &client);
        FillRect(dc, &client, app.toolbarBrush);
        client.bottom = std::min(client.bottom, 80L);
        FillRect(dc, &client, app.toolbarBrush);
        return 1;
    }
    case WM_COMMAND:
        app.handle_command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL:
        if (app.wheel_zoom(pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), GET_WHEEL_DELTA_WPARAM(wParam))) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_NOTIFY: {
        auto const* notification = reinterpret_cast<NMHDR const*>(lParam);
        if (notification && notification->hwndFrom == app.libraryTree) {
            if (notification->code == TVN_ITEMEXPANDINGW) {
                auto* change = reinterpret_cast<NMTREEVIEWW*>(lParam);
                if (change->action == TVE_EXPAND) for (HTREEITEM item = TreeView_GetRoot(app.libraryTree); item; item = TreeView_GetNextSibling(app.libraryTree,item))
                    if (item != change->itemNew.hItem) TreeView_Expand(app.libraryTree,item,TVE_COLLAPSE);
            } else if (notification->code == TVN_SELCHANGEDW) {
                auto const* change = reinterpret_cast<NMTREEVIEWW const*>(lParam);
                if (change->itemNew.lParam > 0)
                    app.choose_tool(static_cast<Tool>(change->itemNew.lParam - 1));
            } else if (notification->code == TVN_GETINFOTIPW) {
                auto* tip = reinterpret_cast<NMTVGETINFOTIPW*>(lParam);
                int index = static_cast<int>(tip->lParam) - 1;
                wchar_t const* description = index == DFlipFlopTool ? L"D\uFF1A\u4E0A\u5347\u6CBF\u91C7\u6837\uFF1B\u5DE6\u4FA7 D/CLK/R\uFF0C\u53F3\u4FA7 Q/~Q\uFF1BR=1 \u5F02\u6B65\u590D\u4F4D\u3002" :
                    index == RSLatchTool ? L"RS\uFF1AS \u7F6E\u4F4D\uFF0CR \u590D\u4F4D\uFF0C\u9AD8\u7535\u5E73\u6709\u6548\uFF0CR \u4F18\u5148\uFF1B\u53F3\u4FA7 Q/~Q\u3002" :
                    index == ClockTool ? L"\u624B\u52A8\u65F6\u949F\uFF1A\u9009\u62E9\u5DE5\u5177\u4E0B\u5355\u51FB\u5207\u6362 0/1\uFF1B0 \u2192 1 \u4E3A\u4E0A\u5347\u6CBF\u3002PCB \u4F7F\u7528\u5916\u90E8\u65F6\u949F\u63A5\u53E3\u3002" :
                    index == ConstantTool ? L"\u903B\u8F91\u5E38\u91CF\uFF1A\u5355\u51FB\u5207\u6362 0/1\uFF0C\u5BFC\u51FA\u540E\u5206\u522B\u63A5 GND/+5V\u3002" :
                    index == HalfAdderTool ? L"\u534A\u52A0\u5668\uFF1A\u4E24\u4E2A\u8F93\u5165\uFF1B\u4E0A\u65B9\u8F93\u51FA\u548C\u4F4D\uFF0C\u4E0B\u65B9\u8F93\u51FA\u8FDB\u4F4D\u3002" :
                    index == HalfSubtractorTool ? L"\u534A\u51CF\u5668\uFF1A\u4E24\u4E2A\u8F93\u5165\uFF1B\u4E0A\u65B9\u8F93\u51FA\u5DEE\u4F4D\uFF0C\u4E0B\u65B9\u8F93\u51FA\u501F\u4F4D\u3002" :
                    index == EqualTool ? L"\u76F8\u7B49\u6BD4\u8F83\u5668\uFF1A\u4E24\u4E2A\u8F93\u5165\u76F8\u540C\u65F6\u8F93\u51FA 1\u3002" :
                    index >= AndTool && index <= XnorTool ? KindNames[index - AndTool] :
                    L"\u5C55\u5F00\u5206\u7C7B\u5E76\u5355\u51FB\u5143\u4EF6\uFF0C\u5728\u753B\u5E03\u4E0A\u653E\u7F6E\u3002";
                lstrcpynW(tip->pszText, description, tip->cchTextMax);
            }
        }
        return 0;
    }
    case WM_MENUSELECT:
        app.show_menu_hint(reinterpret_cast<HMENU>(lParam), LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_EXITMENULOOP:
        app.show_menu_hint(nullptr, 0xFFFF, 0);
        return 0;
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_MENU) { measure_menu_item(*item); return TRUE; }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_DRAWITEM: {
        auto const* item = reinterpret_cast<DRAWITEMSTRUCT const*>(lParam);
        if (item && item->CtlType == ODT_MENU) { draw_menu_item(*item); return TRUE; }
        if (item && item->CtlID >= ToolBase && item->CtlID < ToolBase + 7) {
            draw_toolbar_button(*item);
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_DESTROY:
        if (app.menuBrush) DeleteObject(app.menuBrush);
        if (app.toolbarBrush) DeleteObject(app.toolbarBrush);
        if (app.uiFont) DeleteObject(app.uiFont);
        if (app.gateFont) DeleteObject(app.gateFont);
        PostQuitMessage(0);
        return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

int run(HINSTANCE instance, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW canvasClass{};
    canvasClass.cbSize = sizeof(canvasClass);
    canvasClass.lpfnWndProc = CanvasProcedure;
    canvasClass.hInstance = instance;
    canvasClass.lpszClassName = CanvasClass;
    canvasClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    canvasClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    if (!RegisterClassExW(&canvasClass)) return 1;
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.lpfnWndProc = MainProcedure;
    mainClass.hInstance = instance;
    mainClass.lpszClassName = MainClass;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&mainClass)) return 1;
    int width = std::min(1440, GetSystemMetrics(SM_CXSCREEN) - 80);
    int height = std::min(900, GetSystemMetrics(SM_CYSCREEN) - 100);
    HWND window = CreateWindowExW(0, MainClass, L"\u7B80\u6613 Logisim C++", WS_OVERLAPPEDWINDOW,
                                  (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                  width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    ACCEL shortcuts[] = {
        {FVIRTKEY | FCONTROL, 'N', FileNew}, {FVIRTKEY | FCONTROL, 'O', FileOpen},
        {FVIRTKEY | FCONTROL, 'S', FileSave},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', FileSaveAs},
        {FVIRTKEY | FCONTROL, 'Q', FileExit},
        {FVIRTKEY | FCONTROL, 'Z', EditUndo},
        {FVIRTKEY | FCONTROL, 'A', EditSelectAll},
        {FVIRTKEY, VK_DELETE, EditDelete}, {FVIRTKEY, 'R', EditRotate},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, ZoomIn},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS, ZoomIn},
        {FVIRTKEY | FCONTROL, VK_ADD, ZoomIn},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, ZoomOut},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, ZoomOut},
        {FVIRTKEY | FCONTROL, '0', ZoomReset}
    };
    HACCEL accelerators = CreateAcceleratorTableW(shortcuts, static_cast<int>(sizeof(shortcuts) / sizeof(shortcuts[0])));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accelerators, &message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    DestroyAcceleratorTable(accelerators);
    return static_cast<int>(message.wParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    return run(instance, show);
}

int main() {
    return run(GetModuleHandleW(nullptr), SW_SHOWDEFAULT);
}

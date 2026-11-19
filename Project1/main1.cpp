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
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
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
#endif

namespace {

constexpr int Grid = 20;
constexpr int SidebarWidth = 260;
constexpr COLORREF Blue = RGB(22, 132, 232);
constexpr COLORREF Dark = RGB(39, 49, 59);
constexpr COLORREF MenuSurface = RGB(223, 236, 248);
constexpr COLORREF ToolbarSurface = RGB(235, 243, 251);
constexpr int DirectionX[4] = {1, 0, -1, 0};
constexpr int DirectionY[4] = {0, 1, 0, -1};
constexpr wchar_t const* DirectionNames[4] = {L"右", L"下", L"左", L"上"};
constexpr wchar_t const* KindNames[] = {L"与门", L"或门", L"非门", L"输入引脚", L"输出引脚",
    L"异或门", L"与非门", L"或非门", L"同或门", L"半加器", L"半减器", L"相等比较器"};
constexpr char const* KindKeys[] = {"and", "or", "not", "input", "output",
    "xor", "nand", "nor", "xnor", "half_adder", "half_subtractor", "equal"};

enum Kind { And, Or, Not, Input, Output, Xor, Nand, Nor, Xnor, HalfAdder, HalfSubtractor, Equal };
enum Tool { SelectTool, WireTool, AndTool, OrTool, NotTool, InputTool, OutputTool,
            XorTool, NandTool, NorTool, XnorTool, HalfAdderTool, HalfSubtractorTool, EqualTool };

struct Component {
    int id = 0;
    Kind kind = And;
    int x = 0, y = 0, direction = 0, inputs = 2;
    bool value = false;
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
int input_count(Component const& c) {
    return c.kind == Not || c.kind == Output ? 1 :
           c.kind >= HalfAdder ? 2 : c.inputs;
}
int output_count(Component const& c) {
    return c.kind == HalfAdder || c.kind == HalfSubtractor ? 2 : 1;
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
    int result = 23;
    for (int offset : input_offsets(input_count(c))) result = std::max(result, std::abs(offset) + 12);
    return result;
}

std::vector<Port> ports(Component const& c) {
    std::vector<Port> result;
    int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
    if (c.kind == Input) {
        result.push_back({c.id, -1, pt(c.x + 40 * dx, c.y + 40 * dy)});
    } else if (c.kind == Output) {
        result.push_back({c.id, 0, pt(c.x - 40 * dx, c.y - 40 * dy)});
    } else {
        auto offsets = input_offsets(input_count(c));
        for (int i = 0; i < static_cast<int>(offsets.size()); ++i) {
            result.push_back({c.id, i, pt(c.x - 40 * dx - offsets[i] * dy,
                                           c.y - 40 * dy + offsets[i] * dx)});
        }
        for (int i = 0; i < output_count(c); ++i) {
            int offset = output_count(c) == 1 ? 0 : (i == 0 ? -Grid : Grid);
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
    if (c.kind == Input || c.kind == Output) return RECT{c.x - 20, c.y - 20, c.x + 20, c.y + 20};
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
        if (c.kind == Input || c.kind == Output) {
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

bool near_polyline(std::vector<POINT> const& points, POINT p) {
    for (size_t i = 1; i < points.size(); ++i) {
        POINT a = points[i - 1], b = points[i];
        if (a.x == b.x && std::abs(p.x - a.x) <= 6 && p.y >= std::min(a.y, b.y) - 6 && p.y <= std::max(a.y, b.y) + 6) return true;
        if (a.y == b.y && std::abs(p.y - a.y) <= 6 && p.x >= std::min(a.x, b.x) - 6 && p.x <= std::max(a.x, b.x) + 6) return true;
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
    for (auto const& c : circuit.components) values[c.id] = {c.kind == Input && c.value, false};
    for (size_t pass = 0; pass <= circuit.components.size(); ++pass) {
        bool changed = false;
        for (auto const& c : circuit.components) {
            if (c.kind == Input) continue;
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
            << ", \"value\": " << (c.value ? "true" : "false") << "}";
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
        for (; index < static_cast<int>(std::size(KindKeys)) && kind != KindKeys[index]; ++index) {}
        if (index == static_cast<int>(std::size(KindKeys))) throw std::runtime_error("unknown component");
        c.kind = static_cast<Kind>(index);
        c.x = item.get("x").as_int(); c.y = item.get("y").as_int();
        c.direction = item.get("direction").as_int();
        c.inputs = item.get("inputs").as_int();
        c.value = item.get("value").as_bool();
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
        if (!source || !target || w.source == w.target || source->kind == Output || target->kind == Input ||
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

enum CommandId {
    FileNew = 100, FileOpen, FileSave, FileSaveAs, FileExit,
    EditSelectAll, EditDelete, EditRotate,
    SimToggle, SimReset, HelpAbout, CountMinus, CountPlus,
    ToolBase = 200, CountEdit = 220, DirectionCombo = 221, RotateButton = 222, LibraryTree = 223
};

class App {
public:
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
    std::wstring path;
    std::wstring menuTipText, menuPreviousStatus;
    Tool tool = SelectTool;
    std::set<int> selectedComponents, selectedWires;
    bool updatingControls = false;
    bool draggingComponent = false, dragged = false, marquee = false;
    POINT press{}, dragOffset{}, marqueeStart{}, marqueeEnd{}, hover{};
    bool hoverValid = false;
    bool menuHintActive = false;
    int dragComponentId = 0, wireStartId = 0, wireStartPort = -1, branchFromWireId = 0;
    POINT branchPoint{};
    std::vector<POINT> bends;

    void create_ui(HINSTANCE instance);
    void layout();
    void refresh() { if (canvas) InvalidateRect(canvas, nullptr, FALSE); }
    void set_status(wchar_t const* message) { if (status) SetWindowTextW(status, message); }
    void update_controls();
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
        {L"文件", L"", true, L"新建、打开和保存电路"},
        {L"编辑", L"", true, L"全选、删除或旋转选中对象"},
        {L"项目", L"", true, L"选择门电路或输入、输出引脚"},
        {L"模拟", L"", true, L"切换或重置输入信号"},
        {L"帮助", L"", true, L"查看窗口操作说明"},
        {L"新建", L"Ctrl+N", false, L"清空画布并新建电路"},
        {L"打开…", L"Ctrl+O", false, L"从 JSON 文件打开电路"},
        {L"保存", L"Ctrl+S", false, L"将当前电路保存到文件"},
        {L"另存为…", L"", false, L"选择新文件保存电路"},
        {L"退出", L"", false, L"关闭简易 Logisim"},
        {L"全选", L"Ctrl+A", false, L"选中画布上全部元件和导线"},
        {L"删除选中对象", L"Delete", false, L"删除选中元件和导线"},
        {L"顺时针旋转", L"R", false, L"将选中元件顺时针旋转 90 度"},
        {L"与门", L"", false, L"放置与门：所有输入为真时输出为真"},
        {L"或门", L"", false, L"放置或门：任一输入为真时输出为真"},
        {L"非门", L"", false, L"放置非门：输出为输入的相反值"},
        {L"输入引脚", L"", false, L"放置可单击切换 0/1 的输入引脚"},
        {L"输出引脚", L"", false, L"放置显示电路结果的输出引脚"},
        {L"切换选中输入引脚", L"", false, L"切换选中输入引脚的 0/1 状态"},
        {L"所有输入设为 0", L"", false, L"重置全部输入引脚为 0"},
        {L"使用说明", L"", false, L"查看选择、连线和模拟的操作说明"}
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
    uiFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei UI");
    gateFont = CreateFontW(-29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI Symbol");
    SetMenu(mainWindow, make_menu());
    wchar_t const* labels[7] = {L"选择", L"连线", L"与门", L"或门", L"非门", L"输入引脚", L"输出引脚"};
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
        L"选择：移动元件、选中导线，或从空白处拖动框选",
        L"连线：从输出端或已有导线开始，单击输入端完成连接",
        L"与门：放置可调整输入数量的与门",
        L"或门：放置可调整输入数量的或门",
        L"非门：放置将输入取反的非门",
        L"输入引脚：放置后单击可切换 0/1",
        L"输出引脚：显示连接处的电路结果"
    };
    for (int i = 0; i < 7; ++i) {
        toolButtons[i] = make_control(L"BUTTON", labels[i], BS_OWNERDRAW,
                                      ToolBase + i, mainWindow);
        add_tooltip(toolButtons[i], descriptions[i]);
    }
    libraryTitle = make_control(L"STATIC", L"元件库", SS_LEFT, 0, mainWindow);
    libraryTree = make_control(WC_TREEVIEWW, L"", WS_BORDER | WS_TABSTOP |
                               TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                               TVS_SHOWSELALWAYS | TVS_INFOTIP, LibraryTree, mainWindow);
    SendMessageW(libraryTree, TVM_SETITEMHEIGHT, 30, 0);
    auto insert_library_item = [&](HTREEITEM parent, wchar_t const* text, int toolIndex) {
        TVINSERTSTRUCTW item{};
        item.hParent = parent;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = const_cast<LPWSTR>(text);
        item.item.lParam = toolIndex + 1;
        return TreeView_InsertItem(libraryTree, &item);
    };
    HTREEITEM gates = insert_library_item(TVI_ROOT, L"门电路", -1);
    for (int index : {AndTool, OrTool, NotTool, XorTool, NandTool, NorTool, XnorTool})
        insert_library_item(gates, KindNames[index - AndTool], index);
    HTREEITEM arithmetic = insert_library_item(TVI_ROOT, L"运算器", -1);
    for (int index : {HalfAdderTool, HalfSubtractorTool, EqualTool})
        insert_library_item(arithmetic, KindNames[index - AndTool], index);
    HTREEITEM pins = insert_library_item(TVI_ROOT, L"引脚", -1);
    for (int index : {InputTool, OutputTool})
        insert_library_item(pins, KindNames[index - AndTool], index);
    propertyTitle = make_control(L"STATIC", L"属性", SS_LEFT, 0, mainWindow);
    selectionLabel = make_control(L"STATIC", L"未选中元件", SS_LEFT, 0, mainWindow);
    inputCountLabel = make_control(L"STATIC", L"输入数", SS_LEFT, 0, mainWindow);
    countMinus = make_control(L"BUTTON", L"−", BS_PUSHBUTTON, CountMinus, mainWindow);
    countEdit = make_control(L"EDIT", L"2", WS_BORDER | ES_CENTER | ES_NUMBER, CountEdit, mainWindow);
    countPlus = make_control(L"BUTTON", L"+", BS_PUSHBUTTON, CountPlus, mainWindow);
    directionLabel = make_control(L"STATIC", L"朝向", SS_LEFT, 0, mainWindow);
    directionCombo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                   DirectionCombo, mainWindow);
    for (auto name : DirectionNames) SendMessageW(directionCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    rotateButton = make_control(L"BUTTON", L"顺时针旋转  R", BS_PUSHBUTTON, RotateButton, mainWindow);
    tipsTitle = make_control(L"STATIC", L"操作提示", SS_LEFT, 0, mainWindow);
    tipsText = make_control(L"STATIC", L"Ctrl+A 全选\r\nDelete 删除\r\n拖动框选  输入切换\r\n导线分支  空白折点",
                            SS_LEFT, 0, mainWindow);
    status = make_control(L"STATIC", L"选择工具；单击输入引脚可切换 0 / 1。",
                          SS_LEFT | SS_SUNKEN, 0, mainWindow);
    add_tooltip(countMinus, L"减少选中门电路的输入引脚数量");
    add_tooltip(countEdit, L"输入 2 至 8，设置选中门的输入引脚数量");
    add_tooltip(countPlus, L"增加选中门电路的输入引脚数量");
    add_tooltip(directionCombo, L"设置选中门或引脚的朝向");
    add_tooltip(rotateButton, L"将选中门或引脚顺时针旋转 90 度");
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
    for (int i = 0; i < 7; ++i) MoveWindow(toolButtons[i], 10 + i * 102, 9, 94, 58, TRUE);
    MoveWindow(canvas, SidebarWidth, 80, std::max(1, width - SidebarWidth), std::max(1, height - 114), TRUE);
    MoveWindow(status, 0, height - 34, width, 34, TRUE);
    MoveWindow(libraryTitle, 15, 88, 230, 32, TRUE);
    MoveWindow(libraryTree, 10, 120, 240, 130, TRUE);
    MoveWindow(propertyTitle, 15, 258, 230, 32, TRUE);
    MoveWindow(selectionLabel, 15, 290, 230, 32, TRUE);
    MoveWindow(inputCountLabel, 15, 330, 70, 32, TRUE);
    MoveWindow(countMinus, 85, 325, 28, 36, TRUE);
    MoveWindow(countEdit, 115, 325, 48, 36, TRUE);
    MoveWindow(countPlus, 165, 325, 28, 36, TRUE);
    MoveWindow(directionLabel, 15, 375, 55, 32, TRUE);
    MoveWindow(directionCombo, 72, 370, 170, 160, TRUE);
    MoveWindow(rotateButton, 15, 415, 227, 40, TRUE);
    MoveWindow(tipsTitle, 15, 460, 230, 32, TRUE);
    MoveWindow(tipsText, 15, 492, 230, std::max(30, height - 530), TRUE);
}

void App::update_controls() {
    if (!selectionLabel) return;
    int count = static_cast<int>(selectedComponents.size() + selectedWires.size());
    int single = single_component();
    Component const* component = find_component(circuit, single);
    std::wstring label = count > 1 ? L"已选中 " + std::to_wstring(count) + L" 项" :
                         component ? KindNames[component->kind] : count == 1 ? L"导线" : L"未选中元件";
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
    set_status(next == SelectTool ? L"选择工具；从空白处拖动可框选。" :
               next == WireTool ? L"单击输出端或已有导线开始，再单击输入端；空白处可添加折点。" :
                                  L"在画布上单击以放置元件。");
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
    if (direction >= 0 && direction < 4) set_component_direction(circuit, component->id, direction);
    refresh();
}

void App::toggle_selected() {
    Component* component = find_component(circuit, single_component());
    if (component && component->kind == Input) { component->value = !component->value; refresh(); }
}

void App::reset_inputs() {
    for (auto& c : circuit.components) if (c.kind == Input) c.value = false;
    refresh();
}

Component* App::component_at(POINT p) {
    for (auto it = circuit.components.rbegin(); it != circuit.components.rend(); ++it) {
        auto const& c = *it;
        bool hit = false;
        if (c.kind == Input || c.kind == Output) {
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
    for (auto it = circuit.components.rbegin(); it != circuit.components.rend(); ++it)
        for (auto const& port : ports(*it)) {
            int dx = p.x - port.point.x, dy = p.y - port.point.y;
            if (dx * dx + dy * dy <= 81) { result = port; return true; }
        }
    return false;
}

int App::wire_at(POINT p) {
    for (auto it = circuit.wires.rbegin(); it != circuit.wires.rend(); ++it)
        if (near_polyline(route_wire(circuit, *it), p)) return it->id;
    return 0;
}

void App::canvas_down(POINT p) {
    SetFocus(canvas); SetCapture(canvas);
    press = p; dragged = false;
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
                        set_status(L"已从导线开始分支；单击输入端完成连接。");
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
                    remove_wires(circuit, replaced);
                    Wire wire{circuit.nextWire++, wireStartId,
                        port.component, port.index, bends, branchFromWireId, branchPoint};
                    wire.sourcePort = wireStartPort;
                    circuit.wires.push_back(wire);
                    set_status(L"已连接；蓝色表示真，黑色表示假。");
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
    component->x = snap(p.x - dragOffset.x);
    component->y = snap(p.y - dragOffset.y);
    refresh();
}

void App::canvas_up(POINT p) {
    if (GetCapture() == canvas) ReleaseCapture();
    if (tool == SelectTool && marquee) {
        int dx = p.x - marqueeStart.x, dy = p.y - marqueeStart.y;
        marquee = false;
        if (dx * dx + dy * dy > 16) select_rectangle(marqueeStart, p);
    } else if (tool == SelectTool && draggingComponent && !dragged) {
        Component* component = find_component(circuit, dragComponentId);
        if (component && component->kind == Input) {
            int dx = p.x - component->x, dy = p.y - component->y;
            if (dx * dx + dy * dy <= 22 * 22) component->value = !component->value;
        }
    }
    draggingComponent = false;
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
    else Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

POINT gate_point(POINT center, int direction, int forward, int across) {
    int dx = DirectionX[direction], dy = DirectionY[direction];
    return pt(center.x + forward * dx - across * dy,
              center.y + forward * dy + across * dx);
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
        draw_shape(dc, RECT{center.x - radius, center.y - height,
                            center.x + radius, center.y + height}, false, fill, border, selected);
        RECT label{center.x - radius, center.y - height, center.x + radius, center.y + height};
        HGDIOBJ oldFont = SelectObject(dc, app.uiFont);
        int oldMode = SetBkMode(dc, TRANSPARENT);
        COLORREF oldColor = SetTextColor(dc, border);
        DrawTextW(dc, kind == HalfAdder ? L"Σ" : kind == HalfSubtractor ? L"−" : L"=",
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
    Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
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
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(238, 242, 245));
    HGDIOBJ oldPen = SelectObject(memory, gridPen);
    for (int x = 0; x < client.right; x += Grid) { MoveToEx(memory, x, 0, nullptr); LineTo(memory, x, client.bottom); }
    for (int y = 0; y < client.bottom; y += Grid) { MoveToEx(memory, 0, y, nullptr); LineTo(memory, client.right, y); }
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
        COLORREF fill = value && (c.kind == Input || c.kind == Output) ? RGB(220, 239, 255) :
                        selected ? RGB(234, 244, 255) : RGB(249, 251, 253);
        RECT bounds = component_bounds(c);
        if (c.kind == Input || c.kind == Output) {
            draw_shape(memory, bounds, c.kind == Output, fill, border, selected);
            RECT label = bounds;
            SelectObject(memory, gateFont);
            SetTextColor(memory, value ? Blue : Dark);
            DrawTextW(memory, value ? L"1" : L"0", -1, &label,
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
            if (c.kind == Input || c.kind == Output) {
                int dx = DirectionX[c.direction], dy = DirectionY[c.direction];
                int sign = port.index == -1 ? -1 : 1;
                edge = pt(port.point.x + sign * 20 * dx,
                          port.point.y + sign * 20 * dy);
            } else if (port.index < 0) {
                int across = output_count(c) == 2 ? (port.index == -1 ? -Grid : Grid) : 0;
                edge = gate_point(pt(c.x, c.y), c.direction,
                                  inverted_gate(c.kind) ? 27 : 23, across);
            } else {
                int across = input_offsets(input_count(c))[port.index];
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
    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap); DeleteDC(memory);
}

std::wstring choose_file(HWND owner, bool save) {
    wchar_t buffer[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"简易电路 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) return buffer;
    return {};
}

void App::new_file() {
    circuit = Circuit{};
    path.clear(); clear_selection();
    wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); marquee = false;
    SetWindowTextW(mainWindow, L"简易 Logisim C++");
    update_controls(); refresh();
}

void App::open_file() {
    std::wstring chosen = choose_file(mainWindow, false);
    if (chosen.empty()) return;
    std::string text;
    if (!read_file(chosen, text)) {
        MessageBoxW(mainWindow, L"无法读取电路文件。", L"打开失败", MB_OK | MB_ICONERROR);
        return;
    }
    try {
        Circuit loaded = deserialize(text);
        circuit = std::move(loaded);
    } catch (std::exception const&) {
        MessageBoxW(mainWindow, L"文件格式无效，或不属于简易 Logisim 电路。", L"打开失败", MB_OK | MB_ICONERROR);
        return;
    }
    path = chosen; clear_selection(); wireStartId = branchFromWireId = 0; wireStartPort = -1; bends.clear(); marquee = false;
    SetWindowTextW(mainWindow, (L"简易 Logisim C++ — " + path).c_str());
    update_controls(); refresh();
}

void App::save_file(bool choosePath) {
    std::wstring destination = path;
    if (choosePath || destination.empty()) destination = choose_file(mainWindow, true);
    if (destination.empty()) return;
    if (!write_file(destination, serialize(circuit))) {
        MessageBoxW(mainWindow, L"无法写入电路文件。", L"保存失败", MB_OK | MB_ICONERROR);
        return;
    }
    path = destination;
    SetWindowTextW(mainWindow, (L"简易 Logisim C++ — " + path).c_str());
    set_status(L"已保存电路。");
}

void App::handle_command(int id, int notification) {
    if (id >= ToolBase && id < ToolBase + 7) { choose_tool(static_cast<Tool>(id - ToolBase)); return; }
    switch (id) {
    case FileNew: new_file(); break;
    case FileOpen: open_file(); break;
    case FileSave: save_file(false); break;
    case FileSaveAs: save_file(true); break;
    case FileExit: DestroyWindow(mainWindow); break;
    case EditSelectAll: select_all(); break;
    case EditDelete: delete_selected(); break;
    case EditRotate: case RotateButton: rotate_selected(); break;
    case SimToggle: toggle_selected(); break;
    case SimReset: reset_inputs(); break;
    case CountMinus: change_count(-1); break;
    case CountPlus: change_count(1); break;
    case CountEdit: if (notification == EN_KILLFOCUS) change_count(0); break;
    case DirectionCombo: if (notification == CBN_SELCHANGE) change_direction(); break;
    case HelpAbout:
        MessageBoxW(mainWindow,
            L"选择工具可拖动元件；单击输入引脚切换 0 / 1。\n"
            L"Ctrl+A 全选；从空白处拖动可框选元件和导线。\n"
            L"选中项显示虚线，再次单击导线或点击空白处可取消，Delete 删除。\n\n"
            L"连线工具先点输出端或已有导线，再点输入端，空白处可添加折点。\n"
            L"左侧元件库可选择门电路、半加器、半减器和相等比较器。\n"
            L"半加器和半减器的上方为和/差位，下方为进位/借位。\n"
            L"选中多输入门可调整 2–8 个输入；所有元件可旋转四个方向。\n"
            L"导线沿网格线绘制，蓝色表示真，黑色表示假。",
            L"使用说明", MB_OK | MB_ICONINFORMATION);
        break;
    default: break;
    }
}

LRESULT CALLBACK CanvasProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
    case WM_LBUTTONDOWN: app.canvas_down(point); return 0;
    case WM_MOUSEMOVE: app.canvas_move(point, (wParam & MK_LBUTTON) != 0); return 0;
    case WM_LBUTTONUP: app.canvas_up(point); return 0;
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
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        client.bottom = std::min(client.bottom, 80L);
        FillRect(dc, &client, app.toolbarBrush);
        return 1;
    }
    case WM_COMMAND:
        app.handle_command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_NOTIFY: {
        auto const* notification = reinterpret_cast<NMHDR const*>(lParam);
        if (notification && notification->hwndFrom == app.libraryTree) {
            if (notification->code == TVN_SELCHANGEDW) {
                auto const* change = reinterpret_cast<NMTREEVIEWW const*>(lParam);
                if (change->itemNew.lParam > 0)
                    app.choose_tool(static_cast<Tool>(change->itemNew.lParam - 1));
            } else if (notification->code == TVN_GETINFOTIPW) {
                auto* tip = reinterpret_cast<NMTVGETINFOTIPW*>(lParam);
                int index = static_cast<int>(tip->lParam) - 1;
                wchar_t const* description = index == HalfAdderTool ? L"半加器：两个输入；上方输出和位，下方输出进位。" :
                    index == HalfSubtractorTool ? L"半减器：两个输入；上方输出差位，下方输出借位。" :
                    index == EqualTool ? L"相等比较器：两个输入相同时输出 1。" :
                    index >= AndTool && index <= XnorTool ? KindNames[index - AndTool] :
                    L"展开分类并单击元件，在画布上放置。";
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
    int width = std::min(1150, GetSystemMetrics(SM_CXSCREEN) - 80);
    int height = std::min(760, GetSystemMetrics(SM_CYSCREEN) - 100);
    HWND window = CreateWindowExW(0, MainClass, L"简易 Logisim C++", WS_OVERLAPPEDWINDOW,
                                  (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                  width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    ACCEL shortcuts[] = {
        {FVIRTKEY | FCONTROL, 'N', FileNew}, {FVIRTKEY | FCONTROL, 'O', FileOpen},
        {FVIRTKEY | FCONTROL, 'S', FileSave}, {FVIRTKEY | FCONTROL, 'A', EditSelectAll},
        {FVIRTKEY, VK_DELETE, EditDelete}, {FVIRTKEY, 'R', EditRotate}
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

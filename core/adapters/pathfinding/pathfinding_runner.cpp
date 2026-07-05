#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kWidth = 15;
constexpr uint32_t kHeight = 9;
constexpr uint32_t kReplanTick = 4;
constexpr int kUnreachable = -1;

struct Point {
    int x;
    int y;
};

bool operator==(Point left, Point right) {
    return left.x == right.x && left.y == right.y;
}

struct Grid {
    std::array<bool, kWidth * kHeight> blocked{};

    bool Contains(Point point) const {
        return point.x >= 0 && point.x < static_cast<int>(kWidth) &&
            point.y >= 0 && point.y < static_cast<int>(kHeight);
    }

    size_t Index(Point point) const {
        return static_cast<size_t>(point.x) * kHeight +
            static_cast<size_t>(point.y);
    }

    bool IsOpen(Point point) const {
        return Contains(point) && !blocked[Index(point)];
    }

    void Close(Point point) {
        blocked[Index(point)] = true;
    }
};

Grid BaseGrid() {
    Grid grid;
    for (uint32_t x = 0; x < kWidth; ++x) {
        grid.Close({static_cast<int>(x), 0});
        grid.Close({static_cast<int>(x), static_cast<int>(kHeight - 1)});
    }
    for (uint32_t y = 0; y < kHeight; ++y) {
        grid.Close({0, static_cast<int>(y)});
        grid.Close({static_cast<int>(kWidth - 1), static_cast<int>(y)});
    }
    return grid;
}

std::array<Point, 4> PlannerNeighbors(Point point) {
    return {{
        {point.x + 1, point.y},
        {point.x, point.y + 1},
        {point.x - 1, point.y},
        {point.x, point.y - 1},
    }};
}

std::vector<int> ReverseDistances(const Grid& grid, Point goal) {
    std::vector<int> distance(kWidth * kHeight, kUnreachable);
    if (!grid.IsOpen(goal)) return distance;

    std::queue<Point> pending;
    distance[grid.Index(goal)] = 0;
    pending.push(goal);
    while (!pending.empty()) {
        const Point current = pending.front();
        pending.pop();
        const int next_distance = distance[grid.Index(current)] + 1;
        for (Point neighbor : PlannerNeighbors(current)) {
            if (!grid.IsOpen(neighbor) ||
                distance[grid.Index(neighbor)] != kUnreachable)
                continue;
            distance[grid.Index(neighbor)] = next_distance;
            pending.push(neighbor);
        }
    }
    return distance;
}

std::optional<Point> PlannerNext(
    const Grid& grid,
    const std::vector<int>& distance,
    Point current) {
    const int current_distance = distance[grid.Index(current)];
    if (current_distance <= 0) return std::nullopt;
    for (Point neighbor : PlannerNeighbors(current))
        if (grid.IsOpen(neighbor) &&
            distance[grid.Index(neighbor)] == current_distance - 1)
            return neighbor;
    return std::nullopt;
}

std::array<Point, 4> OracleNeighbors(Point point) {
    return {{
        {point.x, point.y - 1},
        {point.x - 1, point.y},
        {point.x, point.y + 1},
        {point.x + 1, point.y},
    }};
}

std::optional<std::vector<Point>> OracleShortestPath(
    const Grid& grid,
    Point source,
    Point goal) {
    if (!grid.IsOpen(source) || !grid.IsOpen(goal)) return std::nullopt;

    constexpr size_t kNoParent = std::numeric_limits<size_t>::max();
    std::array<size_t, kWidth * kHeight> parent{};
    parent.fill(kNoParent);
    std::array<bool, kWidth * kHeight> visited{};
    std::queue<Point> frontier;
    visited[grid.Index(source)] = true;
    frontier.push(source);

    while (!frontier.empty() && !visited[grid.Index(goal)]) {
        const Point current = frontier.front();
        frontier.pop();
        for (Point candidate : OracleNeighbors(current)) {
            if (!grid.IsOpen(candidate) ||
                visited[grid.Index(candidate)])
                continue;
            visited[grid.Index(candidate)] = true;
            parent[grid.Index(candidate)] = grid.Index(current);
            frontier.push(candidate);
        }
    }
    if (!visited[grid.Index(goal)]) return std::nullopt;

    std::vector<Point> reversed;
    size_t cursor = grid.Index(goal);
    const size_t source_index = grid.Index(source);
    while (cursor != source_index) {
        reversed.push_back({
            static_cast<int>(cursor / kHeight),
            static_cast<int>(cursor % kHeight),
        });
        cursor = parent[cursor];
        if (cursor == kNoParent) return std::nullopt;
    }
    reversed.push_back(source);
    return std::vector<Point>(reversed.rbegin(), reversed.rend());
}

bool ExecuteHop(Point source, Point target) {
    const int dx = target.x - source.x;
    const int dy = target.y - source.y;
    if ((dx != 0) == (dy != 0) ||
        (dx < -1 || dx > 1) ||
        (dy < -1 || dy > 1))
        return false;

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(kWidth, kHeight, 1);
    if (!sandbox.Create(info)) return false;

    TensorlessFpmSandboxNode source_node{};
    source_node.energy_subunits = 1;
    const uint32_t axis = dx == 0 ? 1U : 0U;
    const int direction = dx == 0 ? dy : dx;
    source_node.momentum[axis][axis] = direction;
    const uint64_t source_index = tensorless_adapter::Flat(
        kHeight, 1,
        static_cast<uint32_t>(source.x),
        static_cast<uint32_t>(source.y), 0);
    const uint64_t target_index = tensorless_adapter::Flat(
        kHeight, 1,
        static_cast<uint32_t>(target.x),
        static_cast<uint32_t>(target.y), 0);
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), source_index, &source_node),
            "Tensorless_FpmSandboxSetNode"))
        return false;

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[static_cast<size_t>(source_index)] = 1;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep"))
        return false;

    std::vector<TensorlessFpmSandboxNode> nodes;
    if (!tensorless_adapter::ReadNodes(sandbox.get(), nodes)) return false;
    for (uint64_t index = 0; index < count; ++index) {
        const uint64_t expected_energy = index == target_index ? 1 : 0;
        if (nodes[static_cast<size_t>(index)].energy_subunits !=
            expected_energy)
            return false;
        for (uint32_t row = 0; row < TENSORLESS_FPM_AXES; ++row)
            for (uint32_t column = 0;
                 column < TENSORLESS_FPM_AXES; ++column) {
                const int64_t expected_momentum =
                    index == target_index &&
                    row == axis && column == axis
                    ? direction
                    : 0;
                if (nodes[static_cast<size_t>(index)]
                        .momentum[row][column] != expected_momentum)
                    return false;
            }
    }

    if (stats.tick != 1 ||
        stats.total_energy_subunits != 1 ||
        stats.external_energy_exhaust_subunits != 0 ||
        stats.starvation_deficit_subunits != 0 ||
        !tensorless_adapter::ResidualsAreZero(stats))
        return false;
    for (uint32_t row = 0; row < TENSORLESS_FPM_AXES; ++row)
        for (uint32_t column = 0;
             column < TENSORLESS_FPM_AXES; ++column)
            if (stats.external_momentum_exhaust[row][column] != 0)
                return false;
    return true;
}

std::optional<std::string> RunScenario() {
    const Point source{1, 4};
    const Point goal{13, 4};
    const Point dynamic_obstacle{6, 4};
    Grid grid = BaseGrid();

    std::vector<int> distance = ReverseDistances(grid, goal);
    const auto initial_oracle = OracleShortestPath(grid, source, goal);
    if (!initial_oracle ||
        distance[grid.Index(source)] !=
            static_cast<int>(initial_oracle->size() - 1) ||
        distance[grid.Index(source)] != 12)
        return std::nullopt;

    std::ostringstream output;
    Point current = source;
    uint32_t tick = 0;
    uint32_t epoch = 0;
    int replanned_length = kUnreachable;
    Point initial_next_after_change{};
    while (!(current == goal)) {
        if (tick == kReplanTick) {
            const auto old_next = PlannerNext(grid, distance, current);
            if (!old_next || !(*old_next == dynamic_obstacle))
                return std::nullopt;
            initial_next_after_change = *old_next;
            grid.Close(dynamic_obstacle);
            ++epoch;
            distance = ReverseDistances(grid, goal);
            const auto replanned_oracle =
                OracleShortestPath(grid, current, goal);
            if (!replanned_oracle) return std::nullopt;
            replanned_length =
                static_cast<int>(replanned_oracle->size() - 1);
            if (distance[grid.Index(current)] != replanned_length ||
                replanned_length != 10)
                return std::nullopt;
        }

        const int before = distance[grid.Index(current)];
        const auto next = PlannerNext(grid, distance, current);
        if (!next ||
            !grid.IsOpen(*next) ||
            distance[grid.Index(*next)] != before - 1)
            return std::nullopt;
        if (epoch == 1 &&
            tick == kReplanTick &&
            (*next == initial_next_after_change))
            return std::nullopt;
        if (!ExecuteHop(current, *next)) return std::nullopt;

        output
            << "{\"type\":\"hop\",\"tick\":" << tick + 1
            << ",\"epoch\":" << epoch
            << ",\"from\":[" << current.x << "," << current.y << "]"
            << ",\"to\":[" << next->x << "," << next->y << "]}\n";
        current = *next;
        ++tick;
    }

    Grid unreachable = BaseGrid();
    for (int y = 1; y < static_cast<int>(kHeight - 1); ++y)
        unreachable.Close({7, y});
    const std::vector<int> unreachable_distance =
        ReverseDistances(unreachable, goal);
    const auto unreachable_oracle =
        OracleShortestPath(unreachable, source, goal);
    if (unreachable_distance[unreachable.Index(source)] != kUnreachable ||
        unreachable_oracle)
        return std::nullopt;

    output
        << "{\"type\":\"summary\","
        << "\"adapter\":\"dynamic_pathfinding\","
        << "\"grid\":[15,9],"
        << "\"initial_shortest_length\":12,"
        << "\"replan_tick\":4,"
        << "\"blocked\":[6,4],"
        << "\"replanned_remaining_length\":" << replanned_length << ","
        << "\"executed_hops\":" << tick << ","
        << "\"unreachable_case\":true,"
        << "\"energy_residual\":0,"
        << "\"momentum_residual\":0,"
        << "\"deterministic_replay\":true}\n";
    return output.str();
}

}  // namespace

int main() {
    const auto first = RunScenario();
    const auto second = RunScenario();
    if (!first || !second) {
        std::cerr << "pathfinding adapter verification failed\n";
        return 2;
    }
    if (*first != *second) {
        std::cerr << "pathfinding replay mismatch\n";
        return 3;
    }
    std::cout << *first;
    return 0;
}

/**
 * @file test_probe.cpp
 * @brief FEProbe 单元测试
 */

#include <cassert>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>

#include "FEProbe.h"

static void test_value_at_node()
{
    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[10] = 1.5f;
    field.values[20] = 3.0f;
    field.values[30] = -0.5f;

    auto v = FEProbe::valueAtEntity(field, 20);
    assert(v.valid);
    assert(v.entityId == 20);
    assert(v.location == FieldLocation::Node);
    assert(std::fabs(v.value - 3.0f) < 1e-6f);

    auto miss = FEProbe::valueAtEntity(field, 99);
    assert(!miss.valid);

    printf("  PASS: test_value_at_node\n");
}

static void test_value_at_element()
{
    FEScalarField field;
    field.location = FieldLocation::Element;
    field.values[100] = 42.0f;

    auto v = FEProbe::valueAtEntity(field, 100);
    assert(v.valid);
    assert(v.location == FieldLocation::Element);
    assert(std::fabs(v.value - 42.0f) < 1e-6f);

    printf("  PASS: test_value_at_element\n");
}

static void test_hotspots_descending()
{
    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 10.0f;
    field.values[2] = 50.0f;
    field.values[3] = 30.0f;
    field.values[4] = 20.0f;
    field.values[5] = 40.0f;

    auto top3 = FEProbe::topHotspots(field, 3, true);
    assert(top3.size() == 3);
    assert(top3[0].value >= top3[1].value);
    assert(top3[1].value >= top3[2].value);
    assert(std::fabs(top3[0].value - 50.0f) < 1e-6f);
    assert(std::fabs(top3[1].value - 40.0f) < 1e-6f);
    assert(std::fabs(top3[2].value - 30.0f) < 1e-6f);

    printf("  PASS: test_hotspots_descending\n");
}

static void test_hotspots_ascending()
{
    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 10.0f;
    field.values[2] = 50.0f;
    field.values[3] = 30.0f;

    auto bot2 = FEProbe::topHotspots(field, 2, false);
    assert(bot2.size() == 2);
    assert(bot2[0].value <= bot2[1].value);
    assert(std::fabs(bot2[0].value - 10.0f) < 1e-6f);
    assert(std::fabs(bot2[1].value - 30.0f) < 1e-6f);

    printf("  PASS: test_hotspots_ascending\n");
}

static void test_hotspots_count_exceeds_field()
{
    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 5.0f;
    field.values[2] = 10.0f;

    auto all = FEProbe::topHotspots(field, 100, true);
    assert(all.size() == 2);

    printf("  PASS: test_hotspots_count_exceeds_field\n");
}

static void test_hotspots_empty()
{
    FEScalarField field;
    auto result = FEProbe::topHotspots(field, 5, true);
    assert(result.empty());

    auto result2 = FEProbe::topHotspots(field, 0, true);
    assert(result2.empty());

    printf("  PASS: test_hotspots_empty\n");
}

static void test_path_sampling()
{
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};
    model.nodes[2] = {2, {3.0f, 0.0f, 0.0f}};
    model.nodes[3] = {3, {3.0f, 4.0f, 0.0f}};

    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 100.0f;
    field.values[2] = 200.0f;
    field.values[3] = 300.0f;

    std::vector<int> path = {1, 2, 3};
    auto samples = FEProbe::sampleNodePath(model, field, path);

    assert(samples.size() == 3);

    // 距离单调递增
    assert(samples[0].distance < samples[1].distance);
    assert(samples[1].distance < samples[2].distance);

    // 第一个点距离为0
    assert(std::fabs(samples[0].distance) < 1e-6f);
    // 1→2 距离为 3
    assert(std::fabs(samples[1].distance - 3.0f) < 1e-4f);
    // 2→3 距离为 4，累计 7
    assert(std::fabs(samples[2].distance - 7.0f) < 1e-4f);

    // 值正确
    assert(samples[0].value.valid);
    assert(std::fabs(samples[0].value.value - 100.0f) < 1e-6f);
    assert(std::fabs(samples[2].value.value - 300.0f) < 1e-6f);

    printf("  PASS: test_path_sampling\n");
}

static void test_path_sampling_missing_node()
{
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};
    model.nodes[3] = {3, {6.0f, 0.0f, 0.0f}};

    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 10.0f;
    field.values[3] = 30.0f;

    // node 2 不存在，应被跳过
    std::vector<int> path = {1, 2, 3};
    auto samples = FEProbe::sampleNodePath(model, field, path);
    assert(samples.size() == 2);
    assert(std::fabs(samples[1].distance - 6.0f) < 1e-4f);

    printf("  PASS: test_path_sampling_missing_node\n");
}

static void test_path_sampling_empty()
{
    FEModel model;
    FEScalarField field;
    std::vector<int> path;
    auto samples = FEProbe::sampleNodePath(model, field, path);
    assert(samples.empty());

    printf("  PASS: test_path_sampling_empty\n");
}

static void test_write_path_csv()
{
    std::vector<FEPathSample> samples;
    {
        FEPathSample s;
        s.distance = 0.0f;
        s.position = {0, 0, 0};
        s.value.valid = true;
        s.value.entityId = 1;
        s.value.value = 10.0f;
        samples.push_back(s);
    }
    {
        FEPathSample s;
        s.distance = 5.0f;
        s.position = {3, 4, 0};
        s.value.valid = true;
        s.value.entityId = 2;
        s.value.value = 20.0f;
        samples.push_back(s);
    }

    std::string path = "test_path_output.csv";
    bool ok = FEProbe::writePathSamplesCsv(path, samples);
    assert(ok);

    std::ifstream ifs(path);
    assert(ifs.is_open());

    std::string header;
    std::getline(ifs, header);
    assert(header.find("Distance") != std::string::npos);
    assert(header.find("NodeID") != std::string::npos);

    std::string line1;
    std::getline(ifs, line1);
    assert(!line1.empty());

    std::string line2;
    std::getline(ifs, line2);
    assert(!line2.empty());

    ifs.close();
    std::remove(path.c_str());

    printf("  PASS: test_write_path_csv\n");
}

static void test_write_scalar_field_csv()
{
    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[3] = 30.0f;
    field.values[1] = 10.0f;
    field.values[2] = 20.0f;

    std::string path = "test_scalar_output.csv";
    bool ok = FEProbe::writeScalarFieldCsv(path, field);
    assert(ok);

    std::ifstream ifs(path);
    std::string header;
    std::getline(ifs, header);
    assert(header.find("NodeID") != std::string::npos);

    // ID 应按升序排列
    std::string line1, line2, line3;
    std::getline(ifs, line1);
    std::getline(ifs, line2);
    std::getline(ifs, line3);
    assert(line1[0] == '1');
    assert(line2[0] == '2');
    assert(line3[0] == '3');

    ifs.close();
    std::remove(path.c_str());

    printf("  PASS: test_write_scalar_field_csv\n");
}

int main()
{
    printf("Running FEProbe tests...\n");

    test_value_at_node();
    test_value_at_element();
    test_hotspots_descending();
    test_hotspots_ascending();
    test_hotspots_count_exceeds_field();
    test_hotspots_empty();
    test_path_sampling();
    test_path_sampling_missing_node();
    test_path_sampling_empty();
    test_write_path_csv();
    test_write_scalar_field_csv();

    printf("All FEProbe tests passed!\n");
    return 0;
}

#define CATCH_CONFIG_MAIN
#include <Ioss_ConcreteVariableType.h>
#include <Ioss_Field.h>
#include <Ioss_Map.h>
#include <algorithm>
#include <catch.hpp>
#include <exception>
#include <numeric>
#include <vector>

template <typename INT>
void verify_global_to_local(const Ioss::Map &my_map, const std::vector<INT> &init)
{
  size_t count = my_map.map().size() - 1;
  REQUIRE(count == init.size());

  for (size_t i = 0; i < count; i++) {
    INT global = init[i];
    REQUIRE((size_t)my_map.global_to_local(global) == i + 1);
  }
}

template <typename INT> void test_reorder(Ioss::Map &my_map, std::vector<INT> &init, size_t offset)
{
  // The map coming in has already been defined using 'init' and is
  // sequential from 'offset+1' to 'offset+count+1'
  //
  // Redefine the map with a shuffle of the original ids -- reorder
  // Then map the shuffled ordering back to the original order
  // using the `map_field_to_db_scalar_order` function and verify.

  std::random_shuffle(init.begin(), init.end());
  my_map.set_map(init.data(), init.size(), 0, false);
  REQUIRE(!my_map.is_sequential());

  // Check that we get the *original* ordering back from global_to_local.
  size_t count = init.size();
  for (size_t i = 0; i < count; i++) {
    REQUIRE(my_map.global_to_local(init[i]) == int(init[i] - offset));
  }

  // Check that we get the the `reordered` vector has been put into `db` order.
  std::vector<double> reordered(count);
  my_map.map_field_to_db_scalar_order(init.data(), reordered, 0, count, 1, 0);
  for (size_t i = 0; i < count; i++) {
    REQUIRE(reordered[i] == offset + i + 1);
  }
}

TEST_CASE("test random ids", "[random_ids]")
{
  // Create a map of random ids and make verify global to local works.
  size_t    count = 128;
  Ioss::Map my_map;
  my_map.set_size(count);

  std::vector<int> init(count);
  std::iota(init.begin(), init.end(), 2511);
  std::random_shuffle(init.begin(), init.end());
  for (auto &e : init) {
    e = 11 * e;
  }

  my_map.set_map(init.data(), init.size(), 0, true);

  REQUIRE(!my_map.is_sequential());
  REQUIRE(!my_map.is_sequential(true));
  REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
}

TEST_CASE("test sequential map with offset", "[sequential offset]")
{
  // Simple sequential map 'offset+1 .. offset+1+count'
  // Should verify that reverseMap and reorderMap are empty, but
  // not possible with current API...
  size_t    count = 128;
  Ioss::Map my_map;
  my_map.set_size(count);

  std::vector<int64_t>     init(count);
  std::vector<size_t>      offsets{0, 123, 8589934592};
  std::vector<std::string> sections{"offset0", "offset123", "offsetBIG"};

  for (size_t i = 0; i < offsets.size(); i++) {
    SECTION(sections[i])
    {
      std::size_t offset = offsets[i];
      std::iota(init.begin(), init.end(), offset + 1);

      my_map.set_map(init.data(), init.size(), 0, true);

      REQUIRE(my_map.is_sequential());
      REQUIRE(my_map.is_sequential(true));
      REQUIRE_NOTHROW(verify_global_to_local(my_map, init));

      SECTION("Reorder-1")
      {
        test_reorder(my_map, init, offset);

        SECTION("Reorder-2") { test_reorder(my_map, init, offset); }
      }
    }
  }
}

TEST_CASE("test segmented map creation", "[segment]")
{
  Ioss::Map my_map;
  size_t    segments = 4;

  size_t count = 128;
  my_map.set_size(count);

  size_t seg_size = count / segments;
  CHECK(count % segments == 0);

  std::vector<int> init(count);

  std::vector<size_t>      offsets{0, 123};
  std::vector<std::string> sections{"offset0", "offset123"};

  for (size_t i = 0; i < offsets.size(); i++) {
    SECTION(sections[i])
    {
      std::size_t offset = offsets[i];
      std::iota(init.begin(), init.end(), offset + 1);

      for (size_t j = 0; j < segments; j++) {
        my_map.set_map(&init[j * seg_size], seg_size, j * seg_size, true);
        my_map.set_map(&init[j * seg_size], 0, j * seg_size,
                       true); // make sure handles empty segments
      }

      REQUIRE(my_map.is_sequential());     // Based on m_map[0] setting.
      REQUIRE(my_map.is_sequential(true)); // Based on checking all values.
      REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
    }
  }
}

TEST_CASE("test reverse segmented map creation", "[reverse segment]")
{
  Ioss::Map my_map;
  size_t    segments = 4;

  size_t count = 128;
  my_map.set_size(count);

  size_t seg_size = count / segments;
  CHECK(count % segments == 0);

  std::vector<int> init(count);

  std::vector<size_t>      offsets{0, 123};
  std::vector<std::string> sections{"offset0", "offset123"};

  for (size_t i = 0; i < offsets.size(); i++) {
    SECTION(sections[i])
    {
      std::size_t offset = offsets[i];
      std::iota(init.begin(), init.end(), offset + 1);

      for (size_t j = 0; j < segments; j++) {
        size_t k = segments - j - 1;
        my_map.set_map(&init[k * seg_size], seg_size, k * seg_size, true);
        my_map.set_map(&init[k * seg_size], 0, k * seg_size,
                       true); // make sure handles empty segments
      }

      REQUIRE(my_map.is_sequential());
      REQUIRE(my_map.is_sequential(true));
      REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
    }
  }
}

TEST_CASE("test segment gap", "[segment gap]")
{
  // Each segement is sequential, but there is a gap between each segment.
  // Make sure mapping can detect the gap...
  size_t segments = 4;
  size_t count    = 128;
  size_t seg_size = count / segments;
  CHECK(count % segments == 0);

  Ioss::Map my_map;
  my_map.set_size(count);

  std::vector<int> init(count);

  std::vector<size_t>      offsets{0, 123};
  std::vector<std::string> sections{"offset0", "offset123"};

  for (size_t ii = 0; ii < offsets.size(); ii++) {
    SECTION(sections[ii])
    {
      std::size_t offset = offsets[ii];
      for (size_t j = 0; j < segments; j++) {
        size_t seg_begin = j * seg_size;
        size_t seg_end   = seg_begin + seg_size;

        for (size_t i = seg_begin; i < seg_end; i++) {
          init[i] = i + j + offset + 1;
        }
      }

      for (size_t j = 0; j < segments; j++) {
        size_t i = segments - j - 1;
        my_map.set_map(&init[i * seg_size], seg_size, i * seg_size, true);
      }
      REQUIRE(!my_map.is_sequential());
      REQUIRE(!my_map.is_sequential(true));
      REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
    }
  }
}

TEST_CASE("test small reverse", "[small reverse]")
{
  std::vector<int> init{1, 3};

  size_t    count = init.size();
  Ioss::Map my_map;
  my_map.set_size(count);

  my_map.set_map(&init[1], 1, 1, true);
  my_map.set_map(&init[0], 1, 0, true);

  REQUIRE(!my_map.is_sequential());
  REQUIRE(!my_map.is_sequential(true));
  REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
}

TEST_CASE("test small swap front back", "[swap front back]")
{
  size_t    count = 16;
  Ioss::Map my_map;
  my_map.set_size(count);

  // Two segments each sequential 0..7  8..15
  std::vector<int> init{9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8};

  // Build map with segment 2 first and then segment 1.
  my_map.set_map(&init[8], 8, 8, true);
  my_map.set_map(&init[0], 8, 0, true);

  REQUIRE(!my_map.is_sequential());
  REQUIRE(!my_map.is_sequential(true));
  REQUIRE_NOTHROW(verify_global_to_local(my_map, init));
}

TEST_CASE("test map_data sequential", "[map_data_seq]")
{
  size_t    count = 128;
  Ioss::Map my_map;
  my_map.set_size(count);

  std::vector<int>         init(count);
  std::vector<size_t>      offsets{0, 123};
  std::vector<std::string> sections{"offset0", "offset123"};

  for (size_t ii = 0; ii < offsets.size(); ii++) {
    SECTION(sections[ii])
    {
      std::size_t offset = offsets[ii];
      std::iota(init.begin(), init.end(), offset + 1);

      my_map.set_map(init.data(), init.size(), 0, true);

      REQUIRE(my_map.is_sequential());
      REQUIRE(my_map.is_sequential(true));
      REQUIRE_NOTHROW(verify_global_to_local(my_map, init));

      Ioss::StorageInitializer();
      Ioss::Field int_field("int_field", Ioss::Field::INTEGER, "invalid", Ioss::Field::MESH, count);

      SECTION("explicit map")
      {
        // Now try 'map_data' call which is basically a bulk local-global.
        // Pass in local ids, returns global ids.  So if pass in 1..count, should get back 'init'
        std::vector<int> local(count);
        std::iota(local.begin(), local.end(), 1);
        my_map.map_data(local.data(), int_field, count);
        REQUIRE(init == local);

        SECTION("reverse_map")
        {
          // If now reverse 'local', should get the original 1..count back
          my_map.reverse_map_data(local.data(), int_field, count);
          std::vector<int> seq(count);
          std::iota(seq.begin(), seq.end(), 1);
          REQUIRE(local == seq);
        }
      }

      SECTION("implicit map")
      {
        // Now try 'map_data' call which is basically a bulk local-global.
        // Pass in local ids, returns global ids.  So if pass in 1..count, should get back 'init'
        std::vector<int> local(count);
        my_map.map_implicit_data(local.data(), int_field, count, 0);
        REQUIRE(init == local);
      }
    }
  }
}

TEST_CASE("test map_data random", "[map_data_ran]")
{
  size_t    count = 128;
  Ioss::Map my_map;
  my_map.set_size(count);

  std::vector<int> init(count);
  std::iota(init.begin(), init.end(), 2511);
  std::random_shuffle(init.begin(), init.end());
  for (auto &e : init) {
    e = 11 * e;
  }

  my_map.set_map(init.data(), init.size(), 0, true);

  REQUIRE(!my_map.is_sequential());
  REQUIRE(!my_map.is_sequential(true));
  REQUIRE_NOTHROW(verify_global_to_local(my_map, init));

  Ioss::StorageInitializer();
  Ioss::Field int_field("int_field", Ioss::Field::INTEGER, "invalid", Ioss::Field::MESH, count);

  SECTION("explicit map")
  {
    // Now try 'map_data' call which is basically a bulk local-global.
    // Pass in local ids, returns global ids.  So if pass in 1..count, should get back 'init'
    std::vector<int> local(count);
    std::iota(local.begin(), local.end(), 1);
    my_map.map_data(local.data(), int_field, count);
    REQUIRE(init == local);

    SECTION("reverse_map")
    {
      // If now reverse 'local', should get the original 1..count back
      my_map.reverse_map_data(local.data(), int_field, count);
      std::vector<int> seq(count);
      std::iota(seq.begin(), seq.end(), 1);
      REQUIRE(local == seq);
    }
  }

  SECTION("implicit map")
  {
    // Now try 'map_data' call which is basically a bulk local-global.
    // Pass in local ids, returns global ids.  So if pass in 1..count, should get back 'init'
    std::vector<int> local(count);
    my_map.map_implicit_data(local.data(), int_field, count, 0);
    REQUIRE(init == local);
  }
}

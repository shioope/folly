/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_EXPERIMENTAL_CODING_TEST_UTILS_H
#define FOLLY_EXPERIMENTAL_CODING_TEST_UTILS_H

#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <unordered_set>
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace folly { namespace compression {

template <class URNG>
std::vector<uint32_t> generateRandomList(size_t n, uint32_t maxId, URNG&& g) {
  CHECK_LT(n, 2 * maxId);
  std::uniform_int_distribution<> uid(1, maxId);
  std::unordered_set<uint32_t> dataset;
  while (dataset.size() < n) {
    uint32_t value = uid(g);
    if (dataset.count(value) == 0) {
      dataset.insert(value);
    }
  }

  std::vector<uint32_t> ids(dataset.begin(), dataset.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

inline std::vector<uint32_t> generateRandomList(size_t n, uint32_t maxId) {
  std::mt19937 gen;
  return generateRandomList(n, maxId, gen);
}

inline std::vector<uint32_t> generateSeqList(uint32_t minId, uint32_t maxId,
                                             uint32_t step = 1) {
  CHECK_LE(minId, maxId);
  CHECK_GT(step, 0);
  std::vector<uint32_t> ids;
  ids.reserve((maxId - minId) / step + 1);
  for (uint32_t i = minId; i <= maxId; i += step) {
    ids.push_back(i);
  }
  return ids;
}

inline std::vector<uint32_t> loadList(const std::string& filename) {
  std::ifstream fin(filename);
  std::vector<uint32_t> result;
  uint32_t id;
  while (fin >> id) {
    result.push_back(id);
  }
  return result;
}

template <class Reader, class List>
void testNext(const std::vector<uint32_t>& data, const List& list) {
  Reader reader(list);
  EXPECT_EQ(reader.value(), 0);
  for (size_t i = 0; i < data.size(); ++i) {
    EXPECT_TRUE(reader.next());
    EXPECT_EQ(reader.value(), data[i]);
  }
  EXPECT_FALSE(reader.next());
  EXPECT_EQ(reader.value(), std::numeric_limits<uint32_t>::max());
}

template <class Reader, class List>
void testSkip(const std::vector<uint32_t>& data, const List& list,
              size_t skipStep) {
  CHECK_GT(skipStep, 0);
  Reader reader(list);
  EXPECT_EQ(reader.value(), 0);
  for (size_t i = skipStep - 1; i < data.size(); i += skipStep) {
    EXPECT_TRUE(reader.skip(skipStep));
    EXPECT_EQ(reader.value(), data[i]);
  }
  EXPECT_FALSE(reader.skip(skipStep));
  EXPECT_EQ(reader.value(), std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(reader.next());
}

template <class Reader, class List>
void testSkip(const std::vector<uint32_t>& data, const List& list) {
  for (size_t skipStep = 1; skipStep < 25; ++skipStep) {
    testSkip<Reader, List>(data, list, skipStep);
  }
  for (size_t skipStep = 25; skipStep <= 500; skipStep += 25) {
    testSkip<Reader, List>(data, list, skipStep);
  }
}

template <class Reader, class List>
void testSkipTo(const std::vector<uint32_t>& data, const List& list,
                size_t skipToStep) {
  CHECK_GT(skipToStep, 0);

  Reader reader(list);
  EXPECT_EQ(reader.value(), 0);

  const uint32_t delta = std::max<uint32_t>(1, data.back() / skipToStep);
  uint32_t value = delta;
  auto it = data.begin();
  while (true) {
    it = std::lower_bound(it, data.end(), value);
    if (it == data.end()) {
      EXPECT_FALSE(reader.skipTo(value));
      break;
    }
    EXPECT_TRUE(reader.skipTo(value));
    EXPECT_EQ(reader.value(), *it);
    value = reader.value() + delta;
  }
  EXPECT_EQ(reader.value(), std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(reader.next());
}

template <class Reader, class List>
void testSkipTo(const std::vector<uint32_t>& data, const List& list) {
  for (size_t steps = 10; steps < 100; steps += 10) {
    testSkipTo<Reader, List>(data, list, steps);
  }
  for (size_t steps = 100; steps <= 1000; steps += 100) {
    testSkipTo<Reader, List>(data, list, steps);
  }
  testSkipTo<Reader, List>(data, list, std::numeric_limits<size_t>::max());
  {
    Reader reader(list);
    EXPECT_FALSE(reader.skipTo(data.back() + 1));
    EXPECT_EQ(reader.value(), std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(reader.next());
  }
}

template <class Reader, class List>
void testGoTo(const std::vector<uint32_t>& data, const List& list) {
  std::mt19937 gen;
  std::vector<size_t> is(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    is[i] = i;
  }
  std::shuffle(is.begin(), is.end(), gen);
  if (Reader::EncoderType::forwardQuantum == 0) {
    is.resize(std::min<size_t>(is.size(), 100));
  }

  Reader reader(list);
  EXPECT_TRUE(reader.goTo(0));
  EXPECT_EQ(reader.value(), 0);
  for (auto i : is) {
    EXPECT_TRUE(reader.goTo(i + 1));
    EXPECT_EQ(reader.value(), data[i]);
  }
  EXPECT_FALSE(reader.goTo(data.size() + 1));
  EXPECT_EQ(reader.value(), std::numeric_limits<uint32_t>::max());
}

template <class Reader, class Encoder>
void testEmpty() {
  typename Encoder::CompressedList list;
  const typename Encoder::ValueType* const data = nullptr;
  Encoder::encode(data, 0, list);
  {
    Reader reader(list);
    EXPECT_FALSE(reader.next());
    EXPECT_EQ(reader.size(), 0);
  }
  {
    Reader reader(list);
    EXPECT_FALSE(reader.skip(1));
    EXPECT_FALSE(reader.skip(10));
  }
  {
    Reader reader(list);
    EXPECT_FALSE(reader.skipTo(1));
  }
}

template <class Reader, class Encoder>
void testAll(const std::vector<uint32_t>& data) {
  typename Encoder::CompressedList list;
  Encoder::encode(data.begin(), data.end(), list);
  testNext<Reader>(data, list);
  testSkip<Reader>(data, list);
  testSkipTo<Reader>(data, list);
  testGoTo<Reader>(data, list);
  list.free();
}

template <class Reader, class List>
void bmNext(const List& list, const std::vector<uint32_t>& data,
            size_t iters) {
  if (data.empty()) {
    return;
  }
  for (size_t i = 0, j; i < iters; ) {
    Reader reader(list);
    for (j = 0; reader.next(); ++j, ++i) {
      const uint32_t value = reader.value();
      CHECK_EQ(value, data[j]) << j;
    }
  }
}

template <class Reader, class List>
void bmSkip(const List& list, const std::vector<uint32_t>& data,
            size_t skip, size_t iters) {
  if (skip >= data.size()) {
    return;
  }
  for (size_t i = 0, j; i < iters; ) {
    Reader reader(list);
    for (j = skip - 1; j < data.size(); j += skip, ++i) {
      reader.skip(skip);
      const uint32_t value = reader.value();
      CHECK_EQ(value, data[j]);
    }
  }
}

template <class Reader, class List>
void bmSkipTo(const List& list, const std::vector<uint32_t>& data,
              size_t skip, size_t iters) {
  if (skip >= data.size()) {
    return;
  }
  for (size_t i = 0, j; i < iters; ) {
    Reader reader(list);
    for (j = 0; j < data.size(); j += skip, ++i) {
      reader.skipTo(data[j]);
      const uint32_t value = reader.value();
      CHECK_EQ(value, data[j]);
    }
  }
}

template <class Reader, class List>
void bmGoTo(const List& list, const std::vector<uint32_t>& data,
            const std::vector<size_t>& order, size_t iters) {
  CHECK(!data.empty());
  CHECK_EQ(data.size(), order.size());

  Reader reader(list);
  for (size_t i = 0; i < iters; ) {
    for (size_t j : order) {
      reader.goTo(j + 1);
      const uint32_t value = reader.value();
      CHECK_EQ(value, data[j]);
      ++i;
    }
  }
}

}}  // namespaces

#endif  // FOLLY_EXPERIMENTAL_CODING_TEST_UTILS_H

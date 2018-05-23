/*
 * Copyright (c) 2018 IOTA Stiftung
 * https://github.com/iotaledger/rpchub
 *
 * Refer to the LICENSE file for licensing information
 */


#ifndef HUB_IOTA_API_H_
#define HUB_IOTA_API_H_

#include <chrono>
#include <cstdint>
#include <nonstd/optional.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hub {
namespace iota {

struct Transaction {
 public:
  std::string hash;
  std::string address;
  int64_t value;
  std::chrono::system_clock::time_point timestamp;
  int64_t currentIndex;
  int64_t lastIndex;
  std::string bundleHash;
  std::string trunk;
};

struct NodeInfo {
 public:
  std::string latestMilestone;
  uint64_t latestMilestoneIndex;
  uint64_t latestSolidMilestoneIndex;
};

using Bundle = std::vector<Transaction>;

/// IotaAPI class.
/// Provides an API to the tangle. The following API are available:
/// - isNodeSolid
/// - getBalances
/// - getConfirmedBundlesForAddress
/// - filterConfirmedTails
/// - filterConsistentTails
/// - findTransactions
/// - getNodeInfo
/// - getTrytes
/// - attachToTangle
/// - getTransactionsToApprove
/// - storeTransactions
/// - broadcastTransactions
class IotaAPI {
 public:
  virtual bool isNodeSolid() = 0;

  virtual std::unordered_map<std::string, uint64_t> getBalances(
      const std::vector<std::string>& addresses) = 0;

  virtual std::unordered_multimap<std::string, Bundle>
  getConfirmedBundlesForAddresses(
      const std::vector<std::string>& addresses) = 0;

  virtual std::unordered_set<std::string> filterConfirmedTails(
      const std::vector<std::string>& tails,
      const nonstd::optional<std::string>& reference) = 0;

  virtual std::unordered_set<std::string> filterConsistentTails(
      const std::vector<std::string>& tails) = 0;

  virtual std::vector<std::string> findTransactions(
      nonstd::optional<std::vector<std::string>> addresses,
      nonstd::optional<std::vector<std::string>> bundles) = 0;

  virtual NodeInfo getNodeInfo() = 0;

  virtual std::vector<Transaction> getTrytes(
      const std::vector<std::string>& hashes) = 0;

  virtual std::vector<std::string> attachToTangle(
      const std::string& trunkTransaction, const std::string& branchTransaction,
      size_t minWeightMagnitude, const std::vector<std::string>& trytes) = 0;

  virtual std::pair<std::string, std::string> getTransactionsToApprove(
      size_t depth, const nonstd::optional<std::string>& reference = {}) = 0;

  virtual bool storeTransactions(const std::vector<std::string>& trytes) = 0;

  virtual bool broadcastTransactions(
      const std::vector<std::string>& trytes) = 0;
};
}  // namespace iota
}  // namespace hub

#endif  // HUB_IOTA_API_H_

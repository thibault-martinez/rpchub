// Copyright 2018 IOTA Foundation

#include "hub/service/attachment_service.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>
#include <boost/functional/hash.hpp>
#include <iota/models/bundle.hpp>
#include <iota/models/transaction.hpp>

#include "common/helpers/digest.h"
#include "hub/db/helper.h"
#include "hub/iota/pow.h"

namespace hub {
namespace service {

bool AttachmentService::checkSweepTailsForConfirmation(
    db::Connection& connection, const db::Sweep& sweep,
    const std::vector<std::string>& tails) {
  auto confirmedTails = _api->filterConfirmedTails(tails, {});

  LOG(INFO) << "Sweep: " << sweep.id << " (" << sweep.bundleHash
            << ") has: " << confirmedTails.size() << " confirmed tails.";

  if (confirmedTails.size() == 1) {
    const auto& tail = *confirmedTails.cbegin();
    LOG(INFO) << "Marking tail as confirmed: " << tail;
    connection.markTailAsConfirmed(tail);
    return true;
  } else if (confirmedTails.size() > 1) {
    LOG(FATAL) << "More than one confirmed tail!!!";
  }

  return false;
}

bool AttachmentService::checkForUserReattachment(
    db::Connection& connection, const db::Sweep& sweep,
    const std::vector<std::string>& knownTails) {
  auto bundleTransactionHashes =
      _api->findTransactions({}, std::vector<std::string>{sweep.bundleHash});
  auto bundleTransactions = _api->getTrytes(bundleTransactionHashes);

  // Remove non-tails or tails that we know of
  bundleTransactions.erase(
      std::remove_if(bundleTransactions.begin(), bundleTransactions.end(),
                     [&knownTails](const auto& tx) {
                       return
                           // Not a tail
                           tx.currentIndex != 0 ||
                           // Already know about this one
                           std::find(knownTails.begin(), knownTails.end(),
                                     tx.hash) != knownTails.end();
                     }),
      bundleTransactions.end());

  if (bundleTransactions.size() != 0) {
    // Third-party (i.e. user erattachments!)
    std::vector<std::string> userTails;

    for (const auto& ref : bundleTransactions) {
      userTails.push_back(ref.hash);
    }

    auto confirmedTails = _api->filterConfirmedTails(userTails, {});
    if (confirmedTails.size() != 0) {
      // FIXME what if error logic more than one confirmed tail
      const auto& tail = *confirmedTails.cbegin();
      LOG(INFO) << "Inserting confirmed user-attached tail: " << tail;
      connection.createTail(sweep.id, tail);

      LOG(INFO) << "Marking tail as confirmed: " << tail;
      connection.markTailAsConfirmed(tail);
      return true;
    }

    // This also makes sure that we only add bundles that IRI has seen
    // fully.
    auto consistentTails = _api->filterConsistentTails(userTails);

    if (consistentTails.size() != 0) {
      for (const auto& tail : consistentTails) {
        LOG(INFO) << "Inserting UNconfirmed user-attached tail: " << tail;
        connection.createTail(sweep.id, tail);
      }
    }
  }
  return false;
}

void AttachmentService::reattachSweep(db::Connection& dbConnection,
                                      const iota::POWProvider& powProvider,
                                      const db::Sweep& sweep) {
  auto attachedTrytes = powProvider.performPOW(sweep.trytes);
  // Get tail hash of first tx:
  auto tailHash = iota_digest(attachedTrytes[0].c_str());
  LOG(INFO) << "Reattached sweep " << sweep.id << " as: " << tailHash;

  _api->storeTransactions(attachedTrytes);
  _api->broadcastTransactions(attachedTrytes);

  dbConnection.createTail(sweep.id, tailHash);
}

void AttachmentService::promoteSweep(db::Connection& connection,
                                     const iota::POWProvider& powProvider,
                                     const db::Sweep& sweep,
                                     const hub::crypto::Hash& tailHash) {
  auto toApprove = _api->getTransactionsToApprove(0, {tailHash.str()});
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  IOTA::Models::Bundle bundle;
  IOTA::Models::Transaction otx;

  otx.setAddress(
      "SOME9RANDOM9HUB9BEING9REATTACHED9999999999999999999999999999999999999999"
      "999999999");
  otx.setValue(0);
  otx.setTimestamp(timestamp);

  bundle.addTransaction(otx, 1);
  bundle.addTrytes({""});
  bundle.finalize();

  const auto& tx = bundle.getTransactions()[0];

  auto attachedTrytes =
      powProvider.doPOW({tx.toTrytes()}, toApprove.first, toApprove.second);

  _api->storeTransactions(attachedTrytes);
  _api->broadcastTransactions(attachedTrytes);
  LOG(INFO) << "Issued promotion for sweep " << sweep.id << ": "
            << tx.getHash();
}

bool AttachmentService::doTick() {
  // We're splitting processing out to separate methods on purpose.
  // This means, that we can't really batch API requests across sweeps.
  // Shouldn't matter though as we simply assume that the IOTA node is only used
  // by Hub.

  auto& connection = db::DBManager::get().connection();
  auto& powProvider = iota::POWManager::get().provider();

  auto tickStart = std::chrono::system_clock::now();

  auto milestone = _api->getNodeInfo().latestMilestone;

  // 1. Get Unconfirmed sweeps from database.
  auto unconfirmedSweeps = connection.getUnconfirmedSweeps(tickStart);
  LOG(INFO) << "Found " << unconfirmedSweeps.size() << " unconfirmed sweeps.";

  for (const auto& sweep : unconfirmedSweeps) {
    auto transaction = connection.transaction();

    try {
      // 2. Get (tails, timestamp) for these sweeps
      const auto initialSweepTails = connection.getTailsForSweep(sweep.id);

      // 3. Check if one of the tails is confirmed.
      // 4. If not, check if a user reattachment was confirmed
      if (checkSweepTailsForConfirmation(connection, sweep,
                                         initialSweepTails) ||
          checkForUserReattachment(connection, sweep, initialSweepTails)) {
        goto commit;
      } else {
        // 5. If not, check if at least one of the tails per sweep is still
        //    promotable
        // Requerying list of tails because `checkForUserReattachment` might
        // have added some.
        const auto sweepTails = connection.getTailsForSweep(sweep.id);
        auto consistentTails = _api->filterConsistentTails(sweepTails);
        // 5.1. If yes, pick most recent and promote.
        if (consistentTails.size() > 0) {
          // Get newest tail.
          auto toPromote = std::find_if(sweepTails.begin(), sweepTails.end(),
                                        [&consistentTails](const auto& tail) {
                                          return consistentTails.find(tail) !=
                                                 consistentTails.end();
                                        });

          // Promotion can fail if getTransactionsToApprove fails!
          // In this case, we just catch and rollback.
          promoteSweep(connection, powProvider, sweep,
                       hub::crypto::Hash(*toPromote));
        } else {
          // 6. If not, reattach and commit tail to DB.
          reattachSweep(connection, powProvider, sweep);
        }
      }

      LOG(INFO) << "Sweep " << sweep.id << " is still unconfirmed.";

    commit:
      transaction->commit();
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Sweep " << sweep.id
                 << " failed to commit to DB: " << ex.what();

      try {
        transaction->rollback();
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Sweep " << sweep.id << " rollback failed: " << ex.what();
      }
    }
  }

  return true;
}
}  // namespace service
}  // namespace hub

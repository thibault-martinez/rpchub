// Copyright 2018 IOTA Foundation

#ifndef HUB_SERVICE_ATTACHMENT_SERVICE_H_
#define HUB_SERVICE_ATTACHMENT_SERVICE_H_

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hub/db/db.h"
#include "hub/iota/api.h"
#include "hub/iota/pow.h"
#include "hub/service/scheduled_service.h"

namespace hub {
namespace db {
struct Sweep;
struct SweepTail;
}  // namespace db

namespace service {

class AttachmentService : public ScheduledService {
 public:
  explicit AttachmentService(std::shared_ptr<hub::iota::IotaAPI> api,
                             std::chrono::milliseconds interval)
      : ScheduledService(interval), _api(std::move(api)) {}
  virtual ~AttachmentService() {}

  const std::string name() const override { return "AttachmentService"; }

 protected:
  bool doTick() override;

 protected:
  void reattachSweep(db::Connection& connection,
                     const iota::POWProvider& powProvider,
                     const db::Sweep& sweep);

  void promoteSweep(db::Connection& connection,
                    const iota::POWProvider& powProvider,
                    const db::Sweep& sweep, const hub::crypto::Hash& tailHash);

  bool checkForUserReattachment(db::Connection& connection,
                                const db::Sweep& sweep,
                                const std::vector<std::string>& knownTails);

  bool checkSweepTailsForConfirmation(db::Connection& connection,
                                      const db::Sweep& sweep,
                                      const std::vector<std::string>& tails);

 protected:
  const std::shared_ptr<hub::iota::IotaAPI> _api;
};

}  // namespace service
}  // namespace hub

#endif  // HUB_SERVICE_ATTACHMENT_SERVICE_H_

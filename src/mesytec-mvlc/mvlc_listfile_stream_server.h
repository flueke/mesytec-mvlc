#ifndef A72B02C3_7C5B_4003_84B6_1FFB9C8B489F
#define A72B02C3_7C5B_4003_84B6_1FFB9C8B489F

#include "mesytec-mvlc/mvlc_listfile.h"
#include <memory>
#include <string>

namespace mesytec::mvlc::listfile
{

class MESYTEC_MVLC_EXPORT StreamServerListfileWriteHandle: public WriteHandle
{
  public:
    StreamServerListfileWriteHandle(const std::string &bindAddress = "tcp://*:55333");
    ~StreamServerListfileWriteHandle() override;
    size_t write(const u8 *data, size_t size) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvlc::listfile

#endif /* A72B02C3_7C5B_4003_84B6_1FFB9C8B489F */

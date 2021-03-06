#ifndef SSF_SERVICES_SOCKS_V4_SESSION_H_
#define SSF_SERVICES_SOCKS_V4_SESSION_H_

#include <memory>

#include <boost/noncopyable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>

#include <ssf/network/base_session.h>
#include <ssf/network/socket_link.h>
#include <ssf/network/manager.h>
#include <ssf/network/base_session.h>

#include "ssf/network/socks/v4/request.h"
#include "ssf/network/socks/v4/reply.h"

#include "common/boost/fiber/stream_fiber.hpp"

namespace ssf {
namespace services {
namespace socks {

template <typename Demux>
class SocksServer;

namespace v4 {

template <typename Demux>
class Session : public ssf::BaseSession {
 private:
  using StreamBuf = std::array<char, 50 * 1024>;
  using Tcp = boost::asio::ip::tcp;
  using Fiber = typename boost::asio::fiber::stream_fiber<
      typename Demux::socket_type>::socket;

  using Request = ssf::network::socks::v4::Request;
  using Reply = ssf::network::socks::v4::Reply;

  using Server = SocksServer<Demux>;
  using SocksServerWPtr = std::weak_ptr<Server>;

 public:
  Session(SocksServerWPtr p_socks_server, Fiber client);

 public:
  virtual void start(boost::system::error_code&);

  virtual void stop(boost::system::error_code&);

 private:
  void HandleRequestDispatch(const boost::system::error_code&, std::size_t);

  void DoConnectRequest();

  void DoBindRequest();

  void HandleResolveServerEndpoint(const boost::system::error_code& err,
                                   Tcp::resolver::iterator ep_it);

  void HandleApplicationServerConnect(const boost::system::error_code&);

  void EstablishLink();

  void HandleStop();

 private:
  std::shared_ptr<Session> SelfFromThis() {
    return std::static_pointer_cast<Session>(shared_from_this());
  }

 private:
  boost::asio::io_service& io_service_;
  SocksServerWPtr socks_server_;

  Fiber client_;
  Tcp::socket server_;
  Tcp::resolver server_resolver_;

  Request request_;

  std::shared_ptr<StreamBuf> upstream_;
  std::shared_ptr<StreamBuf> downstream_;
};

template <class VerifyHandler, class StreamSocket>
class ReadRequestCoro : public boost::asio::coroutine {
 public:
  ReadRequestCoro(StreamSocket& c, ssf::network::socks::v4::Request* p_r,
                  VerifyHandler h)
      : c_(c),
        r_(*p_r),
        handler_(h),
        total_length_(0),
        p_stream_(new boost::asio::streambuf) {}

#include <boost/asio/yield.hpp>  // NOLINT
  void operator()(const boost::system::error_code& ec, std::size_t length) {
    if (ec) {
      handler_(ec, total_length_);
      return;
    }

    reenter(this) {
      // Read Request fixed size buffer
      yield boost::asio::async_read(c_, r_.MutBuffer(), std::move(*this));
      total_length_ += length;

      // Read Request variable size name (from now, until '\0')
      yield boost::asio::async_read_until(c_, *p_stream_, '\0',
                                          std::move(*this));
      total_length_ += length;

      // Set the name to complete the request
      r_.set_name(boost::asio::buffer_cast<const char*>(p_stream_->data()));
      p_stream_->consume(length);

      if (r_.Is4aVersion()) {
        // Read Request variable size domain (from now, until '\0')
        yield boost::asio::async_read_until(c_, *p_stream_, '\0',
                                            std::move(*this));
        total_length_ += length;

        // Set the name to complete the request
        r_.set_domain(boost::asio::buffer_cast<const char*>(p_stream_->data()));
      }

      handler_(ec, total_length_);
    }
  }
#include <boost/asio/unyield.hpp>  // NOLINT

 private:
  StreamSocket& c_;
  ssf::network::socks::v4::Request& r_;
  VerifyHandler handler_;
  std::size_t total_length_;
  std::shared_ptr<boost::asio::streambuf> p_stream_;
};

template <class VerifyHandler, class StreamSocket>
void AsyncReadRequest(StreamSocket& c, ssf::network::socks::v4::Request* p_r,
                      VerifyHandler handler) {
  ReadRequestCoro<VerifyHandler, StreamSocket> RequestReader(c, p_r, handler);

  RequestReader(boost::system::error_code(), 0);
}

template <class VerifyHandler, class StreamSocket>
void AsyncSendReply(StreamSocket& c, const ssf::network::socks::v4::Reply& r,
                    VerifyHandler handler) {
  boost::asio::async_write(c, r.ConstBuffer(), handler);
}

}  // v4
}  // socks
}  // services
}  // ssf

#include "services/socks/v4/session.ipp"

#endif  // SSF_SERVICES_SOCKS_V4_SESSION_H_

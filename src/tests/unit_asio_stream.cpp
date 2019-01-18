#include "tests.h"

#include <iostream>

#if defined(BOTAN_HAS_TLS) && defined(BOTAN_HAS_BOOST_ASIO)

#include <botan/asio_stream.h>
#include <botan/tls_callbacks.h>

#endif

namespace Botan_Tests {

#if defined(BOTAN_HAS_TLS) && defined(BOTAN_HAS_BOOST_ASIO)

using namespace std::placeholders;

template <typename T, size_t N>
inline constexpr std::size_t ARR_LEN(const T(&arr) [N]) { return N; }

constexpr uint8_t TEST_DATA[]
   {
   '4','f','8','y','z','s','9','g','2','6','c','v','t','y','q','m',
   'o','v','x','a','3','1','t','m','y','7','n','1','4','t','k','q',
   'r','z','w','0','4','t','c','t','m','u','4','h','l','z','x','f',
   'e','9','b','3','o','j','a','4','o','d','9','j','6','u','f','8',
   '2','d','r','z','n','l','p','7','p','a','1','o','f','z','q','d',
   'x','f','k','8','r','l','a','i','0','b','x','h','2','w','5','w',
   'h','k','h','2','r','8','a','f','d','j','c','0','j','o','k','w',
   'v','4','9','m','s','a','o','f','0','n','u','l','v','z','g','m'
   };
constexpr std::size_t TEST_DATA_SIZE = ARR_LEN(TEST_DATA);

constexpr std::size_t SOCKET_BUF_SIZE = 128;

struct TestState
   {
   std::size_t channel_recv_count = 0;
   std::size_t channel_send_count = 0;

   std::size_t socket_read_count = 0;
   std::size_t socket_write_count = 0;

   std::size_t bytes_read = 0;
   std::size_t bytes_written = 0;

   boost::asio::mutable_buffer socket_buf = boost::asio::buffer(data_, SOCKET_BUF_SIZE);
   uint8_t data_[SOCKET_BUF_SIZE];

   void check_has_read_test_data(
      char* input_buf, std::size_t expected_bytes_read, int expected_socket_read_count, std::size_t start,
      const boost::system::error_code& ec, std::size_t bytes_transferred)
      {
      bytes_read += bytes_transferred;
      result->test_eq("stream consumes correct number of bytes", bytes_read, expected_bytes_read);
      result->test_eq("correct number of read calls on socket", socket_read_count, expected_socket_read_count);
      result->test_is_eq("correct data is written into the buffer",
                         memcmp(input_buf, TEST_DATA + start, bytes_transferred), 0);
      }

   void check_has_written_test_data(
      std::size_t expected_bytes_written, int expected_socket_write_count, std::size_t start,
      const boost::system::error_code& ec, std::size_t bytes_transferred)
      {
      bytes_written += bytes_transferred;
      result->test_eq("stream commits correct number of bytes", bytes_written, expected_bytes_written);
      result->test_eq("data written to socket", socket_write_count, expected_socket_write_count);
      result->test_is_eq("first part of data is written correctly",
                        memcmp(socket_buf.data(), TEST_DATA + start, bytes_transferred), 0);
      }

   Test::Result *result;
   };


class MockChannel
   {
   public:
      MockChannel(Botan::StreamCore& core) : core_(core) {}

   public:
      // mockable channel functions
      //
      std::size_t received_data(const uint8_t buf[], std::size_t buf_size)
         {
         current_test_state_->channel_recv_count++;
         return received_data_fun(buf, buf_size);
         }
      std::function<size_t(const uint8_t buf[], std::size_t buf_size)>
      received_data_fun =
         [](const uint8_t buf[], std::size_t buf_size)
         {
         return buf_size;
         };

      void send(const uint8_t buf[], std::size_t buf_size)
         {
         current_test_state_->channel_send_count++;
         send_data_fun(buf, buf_size);
         }
      std::function<void(const uint8_t buf[], std::size_t buf_size)>
      send_data_fun = [](const uint8_t buf[], std::size_t buf_size) {};

      bool is_active()
         {
         return is_active_fun();
         }
      std::function<bool()> is_active_fun = [] { return false; };

      void set_current_test_state(std::shared_ptr<TestState>& test_state)
         {
         current_test_state_ = test_state;
         }
   public:
      // functions forwarding to callbacks to be triggered from the outside
      //
      void tls_emit_data(const uint8_t buf[], std::size_t buf_size)
         {
         core_.tls_emit_data(buf, buf_size);
         }
      void tls_record_received(uint64_t seq_no, const uint8_t buf[], std::size_t buf_size)
         {
         core_.tls_record_received(seq_no, buf, buf_size);
         }

   protected:
      Botan::StreamCore& core_;
      std::shared_ptr<TestState> current_test_state_;
   };

struct MockSocket
   {
      template <typename ConstBufferSequence>
      std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code&)
         {
         test_state_->socket_write_count++;
         const auto to_write = std::min(boost::asio::buffer_size(buffers), SOCKET_BUF_SIZE);
         auto copied = boost::asio::buffer_copy(test_state_->socket_buf, buffers, to_write);
         return copied;
         }

      template <typename MutableBufferSequence>
      std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec)
         {
         test_state_->socket_read_count++;
         auto read = boost::asio::buffer_copy(buffers, test_state_->socket_buf);
         return read_some_fun(buffers, ec);
         }

      template <typename MutableBufferSequence, typename ReadHandler>
      BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void(boost::system::error_code, std::size_t))
      async_read_some(const MutableBufferSequence& buffers, ReadHandler&& handler)
         {
         boost::system::error_code ec;
         handler(ec, read_some(buffers, ec));
         }

      template <typename ConstBufferSequence, typename WriteHandler>
      BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void(boost::system::error_code, std::size_t))
      async_write_some(const ConstBufferSequence& buffers, WriteHandler&& handler)
         {
         boost::system::error_code ec;
         handler(ec, write_some(buffers, ec));
         }

      std::function<std::size_t(const boost::asio::mutable_buffer&, boost::system::error_code&)>
      read_some_fun = [this](const boost::asio::mutable_buffer&, boost::system::error_code&)
         {
         return SOCKET_BUF_SIZE;
         };

      void reset(std::shared_ptr<TestState>& test_state)
         {
         test_state_ = test_state;
         }

      using lowest_layer_type = MockSocket;
      using executor_type = MockSocket;

   private:
      std::shared_ptr<TestState> test_state_;
   };

static std::shared_ptr<TestState> setupTestHandshake(MockChannel& channel, MockSocket& socket)
   {
   auto s = std::make_shared<TestState>();
   channel.set_current_test_state(s);
   socket.reset(s);

   // fake handshake initialization
   uint8_t buf[128];
   channel.tls_emit_data(buf, sizeof(buf));
   // the channel will claim to be active once it has been given 3 chunks of data
   channel.is_active_fun = [s] { return s->channel_recv_count == 3; };

   return s;
   }

static std::shared_ptr<TestState> setupTestSyncRead(MockChannel& channel, MockSocket& socket, Test::Result &result)
   {
   auto s = std::make_shared<TestState>();
   s->result = &result;
   boost::asio::buffer_copy(s->socket_buf, boost::asio::buffer(TEST_DATA, TEST_DATA_SIZE));

   channel.set_current_test_state(s);
   socket.reset(s);

   channel.received_data_fun = [s, &channel](const uint8_t buf[], std::size_t buf_size)
      {
      if(s->channel_recv_count >= 3)
         {
         channel.tls_record_received(0, buf, buf_size);
         }
      return buf_size;
      };

   return s;
   }

static std::shared_ptr<TestState> setupTestSyncWrite(MockChannel& channel, MockSocket& socket, Test::Result &result)
   {
   auto s = std::make_shared<TestState>();
   s->result = &result;

   channel.set_current_test_state(s);
   socket.reset(s);

   channel.send_data_fun = [s, &channel](const uint8_t buf[], std::size_t buf_size)
      {
      channel.tls_emit_data(buf, buf_size);
      };

   return s;
   }
}

namespace Botan {

/**
 * A specification of StreamBase for the MockChannel used in this test. It
 * matches the specifications for StreamBase<Botan::TLS::Client> and
 * StreamBase<Botan::TLS::Server> except for the underlying channel type and the
 * simplified constructor.
 */
template <>
class StreamBase<Botan_Tests::MockChannel>
   {
   public:
      StreamBase() : channel_(core_) {}

      StreamBase(const StreamBase&) = delete;
      StreamBase& operator=(const StreamBase&) = delete;

   protected:
      StreamCore core_;
      Botan::AutoSeeded_RNG rng_;
      Botan_Tests::MockChannel channel_;
   };

}  // namespace Botan

namespace Botan_Tests {

class ASIO_Stream_Tests final : public Test
   {
      using TestStream = Botan::Stream<MockSocket&, MockChannel>;

      Test::Result test_handshake()
         {
         Test::Result result("TLS handshake");

         MockSocket socket{};
         Botan::Stream<MockSocket&, MockChannel> ssl{socket};
         auto s = setupTestHandshake(ssl.channel(), socket);
         ssl.handshake();

         result.test_gt("writes on socket", s->socket_write_count, 0);
         result.test_eq("feeds data into channel until active", ssl.channel().is_active(), true);
         return result;
         }

      Test::Result test_read_some()
         {
         Test::Result result("synchronous read");

         MockSocket socket{};
         Botan::Stream<MockSocket&, MockChannel> ssl{socket};

         test_read_some_basic_case(result, ssl, socket);
         test_read_some_error(result, ssl, socket);
         test_read_some_reads_data_in_chunks(result, ssl, socket);

         return result;
         }

      Test::Result test_async_read_some()
         {
         Test::Result result("asynchronous read");

         MockSocket socket{};
         Botan::Stream<MockSocket&, MockChannel> ssl{socket};

         test_async_read_some_basic_case(result, ssl, socket);
         // test_async_read_some_error(result, ssl, socket);
         test_async_read_some_reads_data_in_chunks(result, ssl, socket);

         return result;
         }

      void test_read_some_basic_case(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket, result);

         char buf[128];
         boost::system::error_code ec;
         auto bytes_read = boost::asio::read(ssl, boost::asio::buffer(buf, sizeof(buf)), ec);

         result.test_eq("some data is read", s->socket_read_count, 3);
         result.test_is_eq("correct data is read", memcmp(buf, TEST_DATA, sizeof(buf)), 0);
         }

      void test_read_some_error(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket, result);

         socket.read_some_fun = [s, &socket](const boost::asio::mutable_buffer& buffers, boost::system::error_code& ec)
            {
            s->socket_read_count += 1;
            if(s->socket_read_count == 2)
               {
               ec.assign(1, ec.category());
               }
            return SOCKET_BUF_SIZE;
            };

         char buf[128];
         boost::system::error_code ec;
         auto bytes_read = boost::asio::read(ssl, boost::asio::buffer(buf, sizeof(buf)), ec);

         result.test_eq("stream stops reading after error", s->socket_read_count, 2);
         }

      void test_read_some_reads_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket, result);

         socket.read_some_fun = [s, &socket](const boost::asio::mutable_buffer& buffers, boost::system::error_code& ec)
            {
            s->socket_read_count += 1;
            if(s->socket_read_count == 3)
               {
               ec.assign(boost::asio::error::eof, ec.category());
               const std::size_t remaining_bytes = 32;
               return remaining_bytes;
               }
            return SOCKET_BUF_SIZE;
            };

         char buf[48];
         boost::system::error_code ec;

         auto bytes_read = boost::asio::read(ssl, boost::asio::buffer(buf, sizeof(buf)), ec);
         s->check_has_read_test_data(buf, sizeof(buf), 3, 0, ec, bytes_read);

         bytes_read = boost::asio::read(ssl, boost::asio::buffer(buf, sizeof(buf)), ec);
         s->check_has_read_test_data(buf, 2 * sizeof(buf), 3, sizeof(buf), ec, bytes_read);

         bytes_read = boost::asio::read(ssl, boost::asio::buffer(buf, sizeof(buf)), ec);
         s->check_has_read_test_data(buf, SOCKET_BUF_SIZE, 3, 2 * sizeof(buf), ec, bytes_read);
         }

      Test::Result test_write_some()
         {
         Test::Result result("synchronous write_some");

         MockSocket socket{};
         Botan::Stream<MockSocket&, MockChannel> ssl{socket};

         test_write_some_basic_case(result, ssl, socket);
         test_write_some_big_data(result, ssl, socket);
         test_write_some_write_nothing(result, ssl, socket);
         test_write_some_writes_data_in_chunks(result, ssl, socket);

         return result;
         }

      Test::Result test_async_write_some()
         {
         Test::Result result("asynchronous write_some");

         MockSocket socket{};
         Botan::Stream<MockSocket&, MockChannel> ssl{socket};

         test_async_write_some_writes_data_in_chunks(result, ssl, socket);

         return result;
         }

      void test_write_some_basic_case(Test::Result& result, TestStream& ssl,
                                      MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket, result);

         char buf[128];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, sizeof(buf));
         ssl.write_some(buffer, ec);

         result.test_eq("some data is written", s->socket_write_count,
                        ceil((float)buffer.size() / SOCKET_BUF_SIZE));
         result.test_eq("channel calls send", s->channel_send_count, 1);
         }

      void test_write_some_big_data(Test::Result& result, TestStream& ssl,
                                    MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket, result);

         char buf[18 * 1024];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, sizeof(buf));
         ssl.write_some(buffer, ec);

         result.test_eq("big data is written", s->socket_write_count,
                        ceil((float)buffer.size() / SOCKET_BUF_SIZE));
         result.test_eq("channel calls send", s->channel_send_count, 1);
         }

      void test_write_some_write_nothing(Test::Result& result, TestStream& ssl,
                                         MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket, result);

         char buf[128];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, 0);
         ssl.write_some(buffer, ec);

         result.test_eq("no data is written", s->socket_write_count, 0);
         result.test_eq("channel calls send nonetheless", s->channel_send_count, 1);
         }

      void test_write_some_writes_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket, result);

         const std::size_t in_buf_size = 48;
         boost::system::error_code ec;

         auto write_next = [&]
            {
            const std::size_t write = std::min(in_buf_size, TEST_DATA_SIZE - s->bytes_written);
            return ssl.write_some(boost::asio::buffer(TEST_DATA + s->bytes_written, write), ec);
            };

         auto bytes_written = write_next();
         s->check_has_written_test_data(in_buf_size, 1, 0, ec, bytes_written);

         bytes_written = write_next();
         s->check_has_written_test_data(2 * in_buf_size, 2, in_buf_size, ec, bytes_written);

         bytes_written = write_next();
         s->check_has_written_test_data(TEST_DATA_SIZE, 3, 2 * in_buf_size, ec, bytes_written);
         }

      void test_async_write_some_writes_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket, result);

         const std::size_t in_buf_size = 48;
         boost::system::error_code ec;

         std::size_t write = std::min(in_buf_size, TEST_DATA_SIZE - s->bytes_written);
         ssl.async_write_some(boost::asio::buffer(TEST_DATA + s->bytes_written, write),
                              [&](const boost::system::error_code& ec, std::size_t bytes_written)
            {
            s->check_has_written_test_data(in_buf_size, 1, 0, ec, bytes_written);
            });

         write = std::min(in_buf_size, TEST_DATA_SIZE - s->bytes_written);
         ssl.async_write_some(boost::asio::buffer(TEST_DATA + s->bytes_written, write),
                              [&](const boost::system::error_code& ec, std::size_t bytes_written)
            {
            s->check_has_written_test_data(2 * in_buf_size, 2, in_buf_size, ec, bytes_written);
            });

         write = std::min(in_buf_size, TEST_DATA_SIZE - s->bytes_written);
         ssl.async_write_some(boost::asio::buffer(TEST_DATA + s->bytes_written, write),
                              [&](const boost::system::error_code& ec, std::size_t bytes_written)
            {
            s->check_has_written_test_data(TEST_DATA_SIZE, 3, 2 * in_buf_size, ec, bytes_written);
            });
         }

      void test_async_read_some_basic_case(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket, result);

         char buf[128];
         auto read_handler = [&](const boost::system::error_code& ec, std::size_t bytes_transferred)
            {
            std::cout << "channel receive: " << s->channel_recv_count << std::endl;
            result.test_eq("some data is read", s->socket_read_count, 3);
            result.test_is_eq("correct data is read", memcmp(buf, TEST_DATA, sizeof(buf)), 0);
            };

         boost::asio::async_read(ssl, boost::asio::buffer(buf, sizeof(buf)), read_handler);
         }

      void test_async_read_some_reads_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket, result);

         char buf[48];
         boost::system::error_code ec;

         ssl.async_read_some(boost::asio::buffer(buf, sizeof(buf)),
                             [&](const boost::system::error_code& ec, std::size_t bytes_read)
            {
            s->check_has_read_test_data(buf, sizeof(buf), 3, 0, ec, bytes_read);
            });

         ssl.async_read_some(boost::asio::buffer(buf, sizeof(buf)),
                             [&](const boost::system::error_code& ec, std::size_t bytes_read)
            {
            s->check_has_read_test_data(buf, 2 * sizeof(buf), 3, sizeof(buf), ec, bytes_read);
            });

         ssl.async_read_some(boost::asio::buffer(buf, sizeof(buf)),
                             [&](const boost::system::error_code& ec, std::size_t bytes_read)
            {
            s->check_has_read_test_data(buf, SOCKET_BUF_SIZE, 3, 2 * sizeof(buf), ec, bytes_read);
            });
         }


   public:
      std::vector<Test::Result> run() override
         {
         std::vector<Test::Result> results;

         results.push_back(test_handshake());
         results.push_back(test_read_some());
         results.push_back(test_async_read_some());
         results.push_back(test_write_some());
         results.push_back(test_async_write_some());

         return results;
         }
   };

BOTAN_REGISTER_TEST("asio_stream", ASIO_Stream_Tests);

#endif

}  // namespace Botan_Tests

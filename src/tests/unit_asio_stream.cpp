#include "tests.h"

#include <iostream>

#if defined(BOTAN_HAS_TLS)

   #include <botan/asio_stream.h>
   #include <botan/tls_callbacks.h>

#endif

namespace Botan_Tests {

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

#if defined(BOTAN_HAS_TLS)

struct TestState
   {
   std::size_t channel_recv_count = 0;
   std::size_t channel_send_count = 0;

   std::size_t socket_read_count = 0;
   std::size_t socket_write_count = 0;
   };

class MockChannel
   {
   public:
      MockChannel(Botan::detail::StreamCore& core) : core_(core) {}

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
      void tls_record_received(uint64_t seq_no, const uint8_t buf[],
                               std::size_t buf_size)
         {
         core_.tls_record_received(seq_no, buf, buf_size);
         }

   protected:
      Botan::detail::StreamCore& core_;
      std::shared_ptr<TestState> current_test_state_;
   };

struct MockSocket
   {
      static const std::size_t buf_size = 128;
      char socket_buf[buf_size];

      template <typename ConstBufferSequence>
      std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec)
         {
         current_test_state_->socket_write_count++;
         boost::asio::buffer_copy(boost::asio::buffer(socket_buf, buf_size), buffers);
         return std::min(buf_size, boost::asio::buffer_size(buffers));
         }

      template <typename MutableBufferSequence>
      std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec)
         {
         current_test_state_->socket_read_count++;
         boost::asio::buffer_copy(buffers, boost::asio::buffer(TEST_DATA, buf_size));
         return read_some_fun(buffers, ec);
         }

      std::function<std::size_t(const boost::asio::mutable_buffer& buffers,
                                boost::system::error_code& ec)>
      read_some_fun = [this](const boost::asio::mutable_buffer& buffers,
                             boost::system::error_code& ec)
         {
         return buf_size;
         };

      void set_current_test_state(std::shared_ptr<TestState>& test_state)
         {
         current_test_state_ = test_state;
         }

      using lowest_layer_type = MockSocket;
      using executor_type = MockSocket;

   private:
      std::shared_ptr<TestState> current_test_state_;
   };

static std::shared_ptr<TestState> setupTestHandshake(MockChannel& channel,
      MockSocket& socket)
   {
   auto s = std::make_shared<TestState>();
   channel.set_current_test_state(s);
   socket.set_current_test_state(s);

   // fake handshake initialization
   uint8_t buf[128];
   channel.tls_emit_data(buf, sizeof(buf));
   // the channel will claim to be active once it has been given 3 chunks
   // of data
   channel.is_active_fun = [s] { return s->channel_recv_count == 3; };

   return s;
   }

static std::shared_ptr<TestState> setupTestSyncRead(MockChannel& channel,
      MockSocket& socket)
   {
   auto s = std::make_shared<TestState>();
   channel.set_current_test_state(s);
   socket.set_current_test_state(s);

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

static std::shared_ptr<TestState> setupTestSyncWrite(MockChannel& channel,
      MockSocket& socket)
   {
   auto s = std::make_shared<TestState>();
   channel.set_current_test_state(s);
   socket.set_current_test_state(s);

   channel.send_data_fun = [s, &channel](const uint8_t buf[],
                                         std::size_t buf_size)
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
      detail::StreamCore core_;
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
         result.test_eq("feeds data into channel until active",
                        ssl.channel().is_active(), true);
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

      void test_read_some_basic_case(Test::Result& result, TestStream& ssl,
                                     MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket);

         char buf[128];
         boost::system::error_code ec;
         auto bytes_read = ssl.read_some(boost::asio::buffer(buf, sizeof(buf)), ec);

         result.test_eq("some data is read", s->socket_read_count, 3);
         result.test_is_eq("correct data is read", memcmp(buf, TEST_DATA, sizeof(buf)), 0);
         }

      void test_read_some_error(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket);

         socket.read_some_fun = [s, &socket](const boost::asio::mutable_buffer& buffers, boost::system::error_code& ec)
            {
            s->socket_read_count += 1;
            if(s->socket_read_count == 2)
               {
               ec.assign(1, ec.category());
               }
            return socket.buf_size;
            };

         char buf[128];
         boost::system::error_code ec;
         auto bytes_read = ssl.read_some(boost::asio::buffer(buf, sizeof(buf)), ec);

         result.test_eq("stream stops reading after error", s->socket_read_count, 2);
         }

      void test_read_some_reads_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncRead(ssl.channel(), socket);

         char buf[48];
         boost::system::error_code ec;
         size_t total_bytes_read = 0;

         auto bytes_read = ssl.read_some(boost::asio::buffer(buf, sizeof(buf)), ec);
         total_bytes_read += bytes_read;
         result.test_eq("stream consumes some data", total_bytes_read, sizeof(buf));
         result.test_eq("data read from socket", s->socket_read_count, 3);
         result.test_is_eq("first part of data is read correctly", memcmp(buf, TEST_DATA, bytes_read), 0);

         bytes_read = ssl.read_some(boost::asio::buffer(buf, sizeof(buf)), ec);
         total_bytes_read += bytes_read;
         result.test_eq("stream consumes some more data", total_bytes_read, 2 * sizeof(buf));
         result.test_eq("no further data read from socket", s->socket_read_count, 3);
         result.test_is_eq("second part of data is read correctly",
                           memcmp(buf, TEST_DATA + sizeof(buf), bytes_read), 0);

         bytes_read = ssl.read_some(boost::asio::buffer(buf, sizeof(buf)), ec);
         total_bytes_read += bytes_read;
         result.test_eq("stream consumes final data", total_bytes_read, socket.buf_size);
         result.test_eq("still no further data read from socket", s->socket_read_count, 3);
         result.test_is_eq("last part of data is read correctly",
                           memcmp(buf, TEST_DATA + 2 * sizeof(buf), bytes_read), 0);
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

      void test_write_some_basic_case(Test::Result& result, TestStream& ssl,
                                      MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket);

         char buf[128];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, sizeof(buf));
         ssl.write_some(buffer, ec);

         result.test_eq("some data is written", s->socket_write_count,
                        ceil((float)buffer.size() / socket.buf_size));
         result.test_eq("channel calls send", s->channel_send_count, 1);
         }

      void test_write_some_big_data(Test::Result& result, TestStream& ssl,
                                    MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket);

         char buf[18 * 1024];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, sizeof(buf));
         ssl.write_some(buffer, ec);

         result.test_eq("some data is written", s->socket_write_count,
                        ceil((float)buffer.size() / socket.buf_size));
         result.test_eq("channel calls send", s->channel_send_count, 1);
         }

      void test_write_some_write_nothing(Test::Result& result, TestStream& ssl,
                                         MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket);

         char buf[128];
         boost::system::error_code ec;
         auto buffer = boost::asio::buffer(buf, 0);
         ssl.write_some(buffer, ec);

         result.test_eq("no data is written", s->socket_write_count, 0);
         result.test_eq("channel calls send nonetheless", s->channel_send_count, 1);
         }

      void test_write_some_writes_data_in_chunks(Test::Result& result, TestStream& ssl, MockSocket& socket)
         {
         auto s = setupTestSyncWrite(ssl.channel(), socket);

         const std::size_t in_buf_size = 48;
         boost::system::error_code ec;
         size_t total_bytes_written = 0;

         auto write_next = [&]
            {
            const std::size_t write = std::min(in_buf_size, TEST_DATA_SIZE - total_bytes_written);
            return ssl.write_some(boost::asio::buffer(TEST_DATA + total_bytes_written, write), ec);
            };

         auto bytes_written = write_next();
         total_bytes_written += bytes_written;
         result.test_eq("stream consumes some data", total_bytes_written, in_buf_size);
         result.test_eq("data written to socket", s->socket_write_count, 1);
         result.test_is_eq("first part of data is written correctly",
                           memcmp(socket.socket_buf, TEST_DATA, bytes_written), 0);

         bytes_written = write_next();
         total_bytes_written += bytes_written;
         result.test_eq("stream consumes some more data", total_bytes_written, 2 * in_buf_size);
         result.test_eq("no further data written to socket", s->socket_write_count, 2);
         result.test_is_eq("second part of data is written correctly",
                           memcmp(socket.socket_buf, TEST_DATA + in_buf_size, bytes_written), 0);

         bytes_written = write_next();
         total_bytes_written += bytes_written;
         result.test_eq("stream consumes final data", total_bytes_written, TEST_DATA_SIZE);
         result.test_eq("still no further data written to socket", s->socket_write_count, 3);
         result.test_is_eq("last part of data is written correctly",
                           memcmp(socket.socket_buf, TEST_DATA + 2 * in_buf_size, bytes_written), 0);
         }

   public:
      std::vector<Test::Result> run() override
         {
         std::vector<Test::Result> results;

         results.push_back(test_handshake());
         results.push_back(test_read_some());
         results.push_back(test_write_some());

         return results;
         }
   };

#endif

BOTAN_REGISTER_TEST("asio_stream", ASIO_Stream_Tests);
}  // namespace Botan_Tests

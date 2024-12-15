/*!
 * @file
 * @brief
 */

#ifndef GEA2_h
#define GEA2_h

#include <Arduino.h>
#include <cstdint>
#include <memory>

extern "C" {
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_stream_uart.h"
#include "tiny_timer.h"
}

class GEA2 {
 public:
  class Packet {
   public:
    friend class GEA2;

    Packet(uint8_t source, uint8_t destination, std::initializer_list<uint8_t> payload)
    {
      this->rawPacket = std::unique_ptr<char[]>(new char[sizeof(RawPacket) + payload.size()]);
      RawPacket* rawPacket = reinterpret_cast<RawPacket*>(this->rawPacket.get());
      rawPacket->destination = destination;
      rawPacket->payloadLength = payload.size();
      rawPacket->source = source;
      memcpy(rawPacket->payload, payload.begin(), payload.size());
    }

    template <typename T>
    Packet(uint8_t source, uint8_t destination, const T& payload)
    {
      this->rawPacket = std::unique_ptr<char[]>(new char[sizeof(RawPacket) + sizeof(T)]);
      RawPacket* rawPacket = reinterpret_cast<RawPacket*>(this->rawPacket.get());
      rawPacket->destination = destination;
      rawPacket->payloadLength = sizeof(T);
      rawPacket->source = source;
      memcpy(rawPacket->payload, &payload, sizeof(T));
    }

    uint8_t source() const
    {
      return this->getRawPacket()->source;
    }

    uint8_t destination() const
    {
      return this->getRawPacket()->destination;
    }

    uint8_t payloadLength() const
    {
      return this->getRawPacket()->payload_length;
    }

    const uint8_t* payload() const
    {
      return this->getRawPacket()->payload;
    }

   private:
    Packet(const tiny_gea_packet_t* packet)
    {
      this->rawPacket = std::unique_ptr<char[]>(new char[sizeof(RawPacket) + packet->payload_length]);
      RawPacket* rawPacket = reinterpret_cast<RawPacket*>(this->rawPacket.get());
      rawPacket->destination = packet->destination;
      rawPacket->payloadLength = packet->payload_length;
      rawPacket->source = packet->source;
      memcpy(rawPacket->payload, packet->payload, packet->payload_length);
    }

    struct RawPacket {
      uint8_t destination;
      uint8_t payloadLength;
      uint8_t source;
      uint8_t payload[0];
    };

    std::unique_ptr<char[]> rawPacket;

    const tiny_gea_packet_t* getRawPacket() const
    {
      return reinterpret_cast<const tiny_gea_packet_t*>(this->rawPacket.get());
    }
  };

  static constexpr unsigned long baud = 19200;
  static constexpr uint8_t defaultAddress = 0xC0;

  template <typename T>
  class IntegerWrapper {
   public:
    IntegerWrapper()
      : bigEndian(0)
    {
    }

    IntegerWrapper(T value)
    {
      bigEndian = value;

      if(BYTE_ORDER == LITTLE_ENDIAN) {
        swapEndianness(&bigEndian);
      }
    }

    T read() const
    {
      T value = bigEndian;

      if(BYTE_ORDER == LITTLE_ENDIAN) {
        swapEndianness(&value);
      }

      return value;
    }

   private:
    static void swapEndianness(T* t)
    {
      auto rawPointer = reinterpret_cast<uint8_t*>(t);
      for(size_t i = 0; i < sizeof(T) / 2; i++) {
        auto j = sizeof(T) - i - 1;
        auto temp = rawPointer[i];
        rawPointer[i] = rawPointer[j];
        rawPointer[j] = temp;
      }
    }

   private:
    T bigEndian;
  };

  using U8 = IntegerWrapper<uint8_t>;
  using U16 = IntegerWrapper<uint16_t>;
  using U32 = IntegerWrapper<uint32_t>;
  using U64 = IntegerWrapper<uint64_t>;
  using I8 = IntegerWrapper<int8_t>;
  using I16 = IntegerWrapper<int16_t>;
  using I32 = IntegerWrapper<int32_t>;
  using I64 = IntegerWrapper<int64_t>;

  enum class ReadStatus {
    success,
    retriesExhausted
  };

  template <typename T>
  struct ReadResult {
    ReadStatus status;
    T value;
  };

  enum class WriteStatus {
    success,
    retriesExhausted
  };

 private:
  struct PrivatePacketListener {
    PrivatePacketListener(void* context, void (*callback)(void* context, const GEA2::Packet& packet))
      : context(context), callback(callback), subscription()
    {
    }

    void* context;
    void (*callback)(void* context, const GEA2::Packet& packet);
    tiny_event_subscription_t subscription;
  };

 public:
  class PacketListener {
   public:
    PacketListener(PrivatePacketListener* subscription, i_tiny_gea_interface_t* interface)
      : subscription(subscription), interface(interface)
    {
    }

    void cancel()
    {
      if(subscription != nullptr) {
        tiny_event_unsubscribe(tiny_gea_interface_on_receive(interface), &subscription->subscription);
        delete subscription;
        subscription = nullptr;
      }
    }

   private:
    PrivatePacketListener* subscription;
    i_tiny_gea_interface_t* interface;
  };

  void begin(Stream& uart, uint8_t clientAddress = 0xE4, uint32_t requestTimeout = 250, uint8_t requestRetries = 10);
  void loop();

  void sendPacket(const GEA2::Packet& packet);

  PacketListener onPacketReceived(void* context, void (*callback)(void* context, const GEA2::Packet& packet));

  template <typename Context>
  PacketListener onPacketReceived(Context* context, void (*callback)(Context* context, const GEA2::Packet& packet))
  {
    return onPacketReceived(reinterpret_cast<void*>(context), reinterpret_cast<void (*)(void*, const GEA2::Packet& packet)>(callback));
  }

  PacketListener onPacketReceived(void (*callback)(const GEA2::Packet& packet));

  template <typename T>
  void readERDAsync(uint16_t erd, void (*callback)(ReadStatus status, T value))
  {
    readERDAsync(defaultAddress, erd, callback);
  }

  template <typename T>
  void readERDAsync(uint8_t address, uint16_t erd, void (*callback)(ReadStatus status, T value))
  {
    readERDAsync(
      address, erd, reinterpret_cast<void*>(callback), +[](void* context, ReadStatus status, const void* value_, uint8_t valueSize) {
        T value;
        memcpy(&value, value_, std::min(static_cast<size_t>(valueSize), sizeof(T)));
        reinterpret_cast<void (*)(ReadStatus, T)>(context)(status, value);
      });
  }

  template <typename T, typename Context>
  void readERDAsync(uint16_t erd, Context* context, void (*callback)(Context* context, ReadStatus status, T value))
  {
    readERDAsync(defaultAddress, erd, context, callback);
  }

  template <typename T, typename Context>
  void readERDAsync(uint8_t address, uint16_t erd, Context* context, void (*callback)(Context* context, ReadStatus status, T value))
  {
    struct WrappedContext {
      Context* context;
      void (*callback)(Context* context, ReadStatus status, T value);
    };

    auto wrappedContext = new WrappedContext{ context, callback };

    readERDAsync(
      address, erd, reinterpret_cast<void*>(wrappedContext), +[](void* context_, ReadStatus status, const void* value_, uint8_t valueSize) {
        T value;
        memcpy(&value, value_, std::min(static_cast<size_t>(valueSize), sizeof(T)));
        auto* wrappedContext = reinterpret_cast<WrappedContext*>(context_);
        wrappedContext->callback(wrappedContext->context, status, value);
        delete wrappedContext;
      });
  }

  void readERDAsync(uint8_t address, uint16_t erd, void* context, void (*callback)(void* context, ReadStatus status, const void* value, uint8_t valueSize));

  template <typename T>
  ReadResult<T> readERD(uint16_t erd)
  {
    return readERD<T>(defaultAddress, erd);
  }

  template <typename T>
  ReadResult<T> readERD(uint8_t address, uint16_t erd)
  {
    struct Context {
      bool done;
      ReadResult<T> result;
    };

    Context context{ false, ReadResult<T>{ ReadStatus::success, T{} } };

    readERDAsync(
      address, erd, &context, +[](Context* context, ReadStatus status, T value) {
        context->done = true;
        context->result = ReadResult<T>{ status, value };
      });

    while(!context.done) {
      loop();
    }

    return context.result;
  }

  template <typename T>
  void writeERDAsync(uint16_t erd, T value, void (*callback)(WriteStatus status))
  {
    writeERDAsync(defaultAddress, erd, value, callback);
  }

  template <typename T>
  void writeERDAsync(uint8_t address, uint16_t erd, T value, void (*callback)(WriteStatus status))
  {
    writeERDAsync(
      address, erd, &value, sizeof(value), reinterpret_cast<void*>(callback), +[](void* context, WriteStatus status) {
        reinterpret_cast<void (*)(WriteStatus)>(context)(status);
      });
  }

  template <typename T, typename Context>
  void writeERDAsync(uint16_t erd, T value, Context* context, void (*callback)(Context* context, WriteStatus status))
  {
    writeERDAsync(defaultAddress, erd, value, context, callback);
  }

  template <typename T, typename Context>
  void writeERDAsync(uint8_t address, uint16_t erd, T value, Context* context, void (*callback)(Context* context, WriteStatus status))
  {
    writeERDAsync(address, erd, &value, sizeof(value), reinterpret_cast<void*>(context), reinterpret_cast<void (*)(void*, WriteStatus)>(callback));
  }

  void writeERDAsync(uint8_t address, uint16_t erd, const void* value, size_t valueSize, void* context, void (*callback)(void* context, WriteStatus status));

  template <typename T>
  WriteStatus writeERD(uint16_t erd, T value)
  {
    return writeERD(defaultAddress, erd, value);
  }

  template <typename T>
  WriteStatus writeERD(uint8_t address, uint16_t erd, T value)
  {
    return writeERD(address, erd, &value, sizeof(value));
  }

  WriteStatus writeERD(uint8_t address, uint16_t erd, const void* value, size_t valueSize);

 private:
  tiny_timer_group_t timerGroup;

  tiny_stream_uart_t streamUart;

  tiny_event_t fakeMsecInterrupt;
  tiny_timer_t fakeMsecTimer;

  tiny_gea2_interface_t gea2Interface;
  uint8_t sendBuffer[255];
  uint8_t receiveBuffer[255];
  uint8_t sendQueueBuffer[1000];

  tiny_gea2_erd_client_t erdClient;
  tiny_gea2_erd_client_configuration_t clientConfiguration;
  uint8_t clientQueueBuffer[1024];
};

#endif

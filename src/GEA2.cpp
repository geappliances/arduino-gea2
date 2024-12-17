/*!
 * @file
 * @brief
 */

#include "GEA2.h"

extern "C" {
#include "tiny_time_source.h"
}

struct AsyncReadSubscription {
  AsyncReadSubscription(void* context, void (*callback)(void* context, GEA2::ReadStatus status, const void* value, uint8_t valueSize), tiny_gea2_erd_client_request_id_t requestId, tiny_gea2_erd_client_t* erdClient)
    : context(context), callback(callback), requestId(requestId), erdClient(erdClient), subscription()
  {
  }

  void* context;
  void (*callback)(void* context, GEA2::ReadStatus status, const void* value, uint8_t valueSize);
  tiny_gea2_erd_client_request_id_t requestId;
  tiny_gea2_erd_client_t* erdClient;
  tiny_event_subscription_t subscription;
};

struct AsyncWriteSubscription {
  AsyncWriteSubscription(void* context, void (*callback)(void* context, GEA2::WriteStatus status), tiny_gea2_erd_client_request_id_t requestId, tiny_gea2_erd_client_t* erdClient)
    : context(context), callback(callback), requestId(requestId), erdClient(erdClient), subscription()
  {
  }

  void* context;
  void (*callback)(void* context, GEA2::WriteStatus status);
  tiny_gea2_erd_client_request_id_t requestId;
  tiny_gea2_erd_client_t* erdClient;
  tiny_event_subscription_t subscription;
};

void GEA2::begin(Stream& uart, uint8_t clientAddress, uint32_t requestTimeout, uint8_t requestRetries)
{
  auto timeSource = tiny_time_source_init();

  tiny_timer_group_init(&timerGroup, timeSource);

  tiny_event_init(&fakeMsecInterrupt);
  tiny_timer_start_periodic(
    &timerGroup, &fakeMsecTimer, 1, &fakeMsecInterrupt, +[](void* context) {
      auto msecInterrupt = reinterpret_cast<tiny_event_t*>(context);
      tiny_event_publish(msecInterrupt, nullptr);
    });

  tiny_stream_uart_init(&streamUart, &timerGroup, uart);

  tiny_gea2_interface_init(
    &gea2Interface,
    &streamUart.interface,
    timeSource,
    &fakeMsecInterrupt.interface,
    clientAddress,
    sendQueueBuffer,
    sizeof(sendQueueBuffer),
    receiveBuffer,
    sizeof(receiveBuffer),
    false,
    2);

  clientConfiguration.request_timeout = requestTimeout;
  clientConfiguration.request_retries = requestRetries;

  tiny_gea2_erd_client_init(
    &erdClient,
    &timerGroup,
    &gea2Interface.interface,
    clientQueueBuffer,
    sizeof(clientQueueBuffer),
    &clientConfiguration);
}

void GEA2::loop()
{
  tiny_timer_group_run(&timerGroup);
  tiny_gea2_interface_run(&gea2Interface);
}

void GEA2::sendPacket(const GEA2::Packet& packet)
{
  auto rawPacket = packet.getRawPacket();

  struct Context {
    const tiny_gea_packet_t* rawPacket;
  };
  auto context = Context{ rawPacket };

  tiny_gea_interface_send(
    &gea2Interface.interface,
    rawPacket->destination,
    rawPacket->payload_length,
    &context,
    +[](void* _context, tiny_gea_packet_t* packet) {
      auto context = static_cast<Context*>(_context);
      memcpy(packet->payload, context->rawPacket->payload, context->rawPacket->payload_length);
    });
}

GEA2::PacketListener GEA2::onPacketReceived(void* context, void (*callback)(void* context, const GEA2::Packet& packet))
{
  auto subscription = new PrivatePacketListener{
    context,
    callback
  };

  tiny_event_subscription_init(
    &subscription->subscription, subscription, +[](void* context, const void* args_) {
      auto subscription = reinterpret_cast<PrivatePacketListener*>(context);
      auto args = reinterpret_cast<const tiny_gea_interface_on_receive_args_t*>(args_);
      auto packet = Packet(args->packet);
      subscription->callback(subscription->context, packet);
    });
  tiny_event_subscribe(tiny_gea_interface_on_receive(&gea2Interface.interface), &subscription->subscription);

  return PacketListener(subscription, &gea2Interface.interface);
}

GEA2::PacketListener GEA2::onPacketReceived(void (*callback)(const GEA2::Packet& packet))
{
  return onPacketReceived(
    reinterpret_cast<void*>(callback), +[](void* context, const GEA2::Packet& packet) {
      reinterpret_cast<void (*)(const GEA2::Packet& packet)>(context)(packet);
    });
}

void GEA2::readERDAsync(uint8_t address, uint16_t erd, void* context, void (*callback)(void* context, ReadStatus status, const void* value, uint8_t valueSize))
{
  tiny_gea2_erd_client_request_id_t requestId;
  tiny_gea2_erd_client_read(&erdClient.interface, &requestId, address, erd);

  auto subscription = new AsyncReadSubscription{ context, callback, requestId, &erdClient };

  tiny_event_subscription_init(
    &subscription->subscription, subscription, +[](void* context, const void* args_) {
      auto subscription = reinterpret_cast<AsyncReadSubscription*>(context);
      auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(args_);

      if((args->type == tiny_gea2_erd_client_activity_type_read_completed) && (args->read_completed.request_id == subscription->requestId)) {
        subscription->callback(subscription->context, ReadStatus::success, args->read_completed.data, args->read_completed.data_size);
        tiny_event_unsubscribe(tiny_gea2_erd_client_on_activity(&subscription->erdClient->interface), &subscription->subscription);
        delete subscription;
      }
      else if((args->type == tiny_gea2_erd_client_activity_type_read_failed) && (args->read_failed.request_id == subscription->requestId)) {
        switch(args->read_failed.reason) {
          default:
          case tiny_gea2_erd_client_read_failure_reason_retries_exhausted:
            subscription->callback(subscription->context, ReadStatus::retriesExhausted, nullptr, 0);
            break;
        }

        tiny_event_unsubscribe(tiny_gea2_erd_client_on_activity(&subscription->erdClient->interface), &subscription->subscription);
        delete subscription;
      }
    });
  tiny_event_subscribe(tiny_gea2_erd_client_on_activity(&erdClient.interface), &subscription->subscription);
}

void GEA2::writeERDAsync(uint8_t address, uint16_t erd, const void* value, size_t valueSize, void* context, void (*callback)(void* context, WriteStatus status))
{
  tiny_gea2_erd_client_request_id_t requestId;
  tiny_gea2_erd_client_write(&erdClient.interface, &requestId, address, erd, value, valueSize);

  auto subscription = new AsyncWriteSubscription(context, callback, requestId, &erdClient);

  tiny_event_subscription_init(
    &subscription->subscription, subscription, +[](void* context, const void* args_) {
      auto subscription = reinterpret_cast<AsyncWriteSubscription*>(context);
      auto args = reinterpret_cast<const tiny_gea2_erd_client_on_activity_args_t*>(args_);

      if((args->type == tiny_gea2_erd_client_activity_type_write_completed) && (args->write_completed.request_id == subscription->requestId)) {
        subscription->callback(subscription->context, WriteStatus::success);
        tiny_event_unsubscribe(tiny_gea2_erd_client_on_activity(&subscription->erdClient->interface), &subscription->subscription);
        delete subscription;
      }
      else if((args->type == tiny_gea2_erd_client_activity_type_write_failed) && (args->write_failed.request_id == subscription->requestId)) {
        switch(args->write_failed.reason) {
          default:
          case tiny_gea2_erd_client_write_failure_reason_retries_exhausted:
            subscription->callback(subscription->context, WriteStatus::retriesExhausted);
            break;
        }

        tiny_event_unsubscribe(tiny_gea2_erd_client_on_activity(&subscription->erdClient->interface), &subscription->subscription);
        delete subscription;
      }
    });
  tiny_event_subscribe(tiny_gea2_erd_client_on_activity(&erdClient.interface), &subscription->subscription);
}

GEA2::WriteStatus GEA2::writeERD(uint8_t address, uint16_t erd, const void* value, size_t valueSize)
{
  struct Context {
    bool done;
    WriteStatus status;
  };

  Context context{ false, WriteStatus::success };

  writeERDAsync(
    address, erd, value, valueSize, reinterpret_cast<void*>(&context), +[](void* context_, WriteStatus status) {
      Context* context = reinterpret_cast<Context*>(context_);
      context->done = true;
      context->status = status;
    });

  while(!context.done) {
    loop();
  }

  return context.status;
}

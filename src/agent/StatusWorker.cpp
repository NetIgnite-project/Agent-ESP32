#include "agent/StatusWorker.h"

#include "net/Probes.h"

namespace {
constexpr UBaseType_t QUEUE_LENGTH = 8;
constexpr uint32_t TASK_STACK = 6144;
}  // namespace

void StatusWorker::begin() {
	jobs_ = xQueueCreate(QUEUE_LENGTH, sizeof(Job*));
	results_ = xQueueCreate(QUEUE_LENGTH, sizeof(Done));
	xTaskCreate(run, "status", TASK_STACK, this, 2, nullptr);
}

bool StatusWorker::submit(uint32_t requestId, uint32_t generation, const netignite::protocol::StatusRequest& request) {
	auto* job = new Job{requestId, generation, request};
	if (xQueueSend(jobs_, &job, 0) != pdTRUE) {
		delete job;
		return false;
	}
	return true;
}

bool StatusWorker::poll(Done& out) {
	return xQueueReceive(results_, &out, 0) == pdTRUE;
}

void StatusWorker::run(void* arg) {
	auto* self = static_cast<StatusWorker*>(arg);
	for (;;) {
		Job* job = nullptr;
		if (xQueueReceive(self->jobs_, &job, portMAX_DELAY) != pdTRUE || !job) continue;

		const probes::Result result = probes::check(job->request);

		Done done = {};
		done.requestId = job->requestId;
		done.generation = job->generation;
		done.online = result.online;
		strlcpy(done.error, result.error.c_str(), sizeof(done.error));
		delete job;

		// The main loop drains results continuously; drop one if it ever falls behind.
		if (xQueueSend(self->results_, &done, pdMS_TO_TICKS(1000)) != pdTRUE) {
			Done stale;
			xQueueReceive(self->results_, &stale, 0);
			xQueueSend(self->results_, &done, 0);
		}
	}
}

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <nan.h>
#include <node.h>
#include <thread>
#include <uv.h>

using namespace v8;

class Addon {
  public:
    Addon(Isolate *isolate)
      : mutex(new std::mutex),
        conditionVar(new std::condition_variable),
        shutdown(false),
        refCount(0),
        async(new uv_async_t),
        thread([this]() {
          for ( ; ; ) {
            Nan::AsyncWorker *worker = TakeWork();
            if (worker == nullptr) {
              return;
            }

            worker->Execute();
            WorkerCompleted(worker);
          }
        })
    {
      uv_async_init(node::GetCurrentEventLoop(isolate), async, RunCallbacks);
      async->data = this;
      uv_unref((uv_handle_t *)async);

      node::AddEnvironmentCleanupHook(isolate, CleanupAddon, this);
    }

    void QueueWorker(Nan::AsyncWorker *worker) {
      std::lock_guard<std::mutex> lock(*mutex);
      uv_ref((uv_handle_t *)async);
      refCount++;
      workQueue.push(worker);
      conditionVar->notify_one();
    }

    void WorkerCompleted(Nan::AsyncWorker *worker) {
      std::lock_guard<std::mutex> lock(*mutex);
      completedQueue.push(worker);
      uv_async_send(async);
    }

    Nan::AsyncWorker *TakeWork() {
      std::unique_lock<std::mutex> lock(*mutex);
      while (!shutdown && workQueue.empty()) conditionVar->wait(lock);
      if (shutdown) {
        return nullptr;
      }

      Nan::AsyncWorker *worker = workQueue.front();
      workQueue.pop();
      return worker;
    }

    static void RunCallbacks(uv_async_t *handle) {
      if (handle->data) {
        static_cast<Addon *>(handle->data)->RunCallbacks();
      }
    }

    void RunCallbacks() {
      Nan::HandleScope scope;
      v8::Local<v8::Context> context = Nan::GetCurrentContext();
      node::CallbackScope callbackScope(context->GetIsolate(), Nan::New<v8::Object>(), {0, 0});

      std::unique_lock<std::mutex> lock(*mutex);
      Nan::AsyncWorker *worker = completedQueue.front();
      completedQueue.pop();

      lock.unlock();
      worker->WorkComplete();
      worker->Destroy();
      lock.lock();


      if (!completedQueue.empty()) {
        uv_async_send(async);
      }

      if (--refCount == 0) {
        uv_unref((uv_handle_t *)async);
      }
    }

    static void CleanupAddon(void *data) {
      Addon *addon = static_cast<Addon *>(data);

      addon->CleanupAddon();

      delete addon;
    }

    void CleanupAddon() {
      std::queue<Nan::AsyncWorker *> cancelledWorkQueue;
      std::queue<Nan::AsyncWorker *> cancelledCompletedQueue;
      {
        std::lock_guard<std::mutex> lock(*mutex);

        shutdown = true;

        workQueue.swap(cancelledWorkQueue);
        completedQueue.swap(cancelledCompletedQueue);

        refCount = 0;
        uv_unref((uv_handle_t *)async);

        conditionVar->notify_all();
      }

      Nan::HandleScope scope;

      while (cancelledWorkQueue.size()) {
        Nan::AsyncWorker *worker = cancelledWorkQueue.front();
        cancelledWorkQueue.pop();

        worker->WorkComplete();
        worker->Destroy();
      }

      while (cancelledCompletedQueue.size()) {
        Nan::AsyncWorker *worker = cancelledCompletedQueue.front();
        cancelledCompletedQueue.pop();

        worker->WorkComplete();
        worker->Destroy();
      }

      thread.join();

      std::lock_guard<std::mutex> lock(*mutex);
      while (completedQueue.size()) {
        Nan::AsyncWorker *worker = completedQueue.front();
        completedQueue.pop();

        worker->WorkComplete();
        worker->Destroy();
      }

      uv_close((uv_handle_t*)async, nullptr);
      // Hey watch out we should clean this up some day
    }

  private:
    std::unique_ptr<std::mutex> mutex;
    std::unique_ptr<std::condition_variable> conditionVar;
    std::queue<Nan::AsyncWorker *> workQueue;
    std::queue<Nan::AsyncWorker *> completedQueue;
    bool shutdown;
    size_t refCount;
    uv_async_t *async;
    std::thread thread;
};

class DoAsyncThingWorker : public Nan::AsyncWorker {
public:
  DoAsyncThingWorker(Nan::Callback *callback)
    : Nan::AsyncWorker(callback, "DoAsyncThingWorker")
  {}

  void Execute() {
    std::cout << "Entering execute thread" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cout << "Leaving execute thread" << std::endl;
  }

  void HandleOKCallback() {
    std::cout << "HandleOKCallback" << std::endl;
  }
};

NAN_METHOD(DoAsyncThing) {
  Addon *addon = reinterpret_cast<Addon *>(info.Data().As<External>()->Value());
  addon->QueueWorker(new DoAsyncThingWorker(new Nan::Callback(Local<Function>::Cast(info[0]))));
}

NAN_MODULE_INIT(Init) {
  Local<Context> context = Nan::GetCurrentContext();
  Isolate *isolate = context->GetIsolate();
  Local<External> addon = External::New(isolate, new Addon(isolate));

  Nan::Set(
    target,
    Nan::New<String>("doAsyncThing").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(DoAsyncThing, addon)).ToLocalChecked()
  );
}

NAN_MODULE_WORKER_ENABLED(addon, Init)

#include <nan.h>
#include <node.h>
#include <chrono>
#include <thread>
#include <iostream>

using namespace v8;

class DoAsyncThingWorker : public Nan::AsyncWorker {
public:
  DoAsyncThingWorker(Nan::Callback *callback)
    : Nan::AsyncWorker(callback, "DoAsyncThingWorker")
  {}

  void Execute() {
    std::cout << "Entering execute thread" << std::endl;
    std::this_thread::sleep_for(std::chrono::minutes(1));
    std::cout << "Leaving execute thread" << std::endl;
  }

  void HandleOKCallback() {
    std::cout << "HandleOKCallback" << std::endl;
  }
};

NAN_METHOD(DoAsyncThing) {
  Nan::AsyncQueueWorker(new DoAsyncThingWorker(new Nan::Callback(Local<Function>::Cast(info[0]))));
}

NAN_MODULE_INIT(Init) {
  Nan::Set(
    target,
    Nan::New<String>("doAsyncThing").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(DoAsyncThing)).ToLocalChecked()
  );
}

NODE_MODULE(addon, Init)

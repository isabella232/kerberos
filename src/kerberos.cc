#include <memory>

#include "kerberos.h"
#include "common.h"
#include "kerberos_client.h"
#include "kerberos_server.h"

using v8::FunctionTemplate;

#define GSS_MECH_OID_KRB5 9
#define GSS_MECH_OID_SPNEGO 6

static char krb5_mech_oid_bytes [] = "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02";
gss_OID_desc krb5_mech_oid = { 9, &krb5_mech_oid_bytes };

static char spnego_mech_oid_bytes[] = "\x2b\x06\x01\x05\x05\x02";
gss_OID_desc spnego_mech_oid = { 6, &spnego_mech_oid_bytes };

class InitializeClientWorker : public Nan::AsyncWorker {
 public:
  InitializeClientWorker(std::string service, std::string principal, long int gss_flags, gss_OID mech_oid, Nan::Callback *callback)
    : AsyncWorker(callback, "kerberos:InitializeClientWorker"),
      _service(service),
      _principal(principal),
      _gss_flags(gss_flags),
      _mech_oid(mech_oid),
      _client_state(NULL)
  {}

  virtual void Execute() {
    std::unique_ptr<gss_client_state, FreeDeleter> state(gss_client_state_new());
    std::unique_ptr<gss_result, FreeDeleter> result(
      authenticate_gss_client_init(_service.c_str(), _principal.c_str(), _gss_flags, _mech_oid, state.get()));

    if (result->code == AUTH_GSS_ERROR) {
      SetErrorMessage(result->message);
      return;
    }

    _client_state = state.release();
  }

 protected:
  virtual void HandleOKCallback() {
    Nan::HandleScope scope;
    v8::Local<v8::Object> client = KerberosClient::NewInstance(_client_state);
    v8::Local<v8::Value> argv[] = { Nan::Null(), client };
    callback->Call(2, argv, async_resource);
  }

 private:
  std::string _service;
  std::string _principal;
  long int _gss_flags;
  gss_OID _mech_oid;
  gss_client_state* _client_state;
};

NAN_METHOD(InitializeClient) {
  std::string service(*Nan::Utf8String(info[0]));
  v8::Local<v8::Object> options = Nan::To<v8::Object>(info[1]).ToLocalChecked();
  Nan::Callback* callback = new Nan::Callback(Nan::To<v8::Function>(info[2]).ToLocalChecked());

  std::string principal = StringOptionValue(options, "principal");
  uint32_t gss_flags = UInt32OptionValue(options, "gssFlags", 0);
  uint32_t mech_oid_int = UInt32OptionValue(options, "mechOID", 0);
  gss_OID mech_oid = GSS_C_NO_OID;
  if (mech_oid_int == GSS_MECH_OID_KRB5) {
    mech_oid = &krb5_mech_oid;
  } else if (mech_oid_int == GSS_MECH_OID_SPNEGO) {
    mech_oid = &spnego_mech_oid;
  }

  AsyncQueueWorker(new InitializeClientWorker(service, principal, gss_flags, GSS_C_NO_OID, callback));
}

class InitializeServerWorker : public Nan::AsyncWorker {
 public:
  InitializeServerWorker(std::string service, Nan::Callback *callback)
    : AsyncWorker(callback, "kerberos:InitializeServerWorker"),
      _service(service),
      _server_state(NULL)
  {}

  virtual void Execute() {
    std::unique_ptr<gss_server_state, FreeDeleter> state(gss_server_state_new());
    std::unique_ptr<gss_result, FreeDeleter> result(
      authenticate_gss_server_init(_service.c_str(), state.get()));

    if (result->code == AUTH_GSS_ERROR) {
      SetErrorMessage(result->message);
      return;
    }

    _server_state = state.release();
  }

 protected:
  virtual void HandleOKCallback() {
    Nan::HandleScope scope;
    v8::Local<v8::Object> server = KerberosServer::NewInstance(_server_state);
    v8::Local<v8::Value> argv[] = { Nan::Null(), server };
    callback->Call(2, argv, async_resource);
  }

 private:
  std::string _service;
  gss_server_state* _server_state;
};

NAN_METHOD(InitializeServer) {
  std::string service(*Nan::Utf8String(info[0]));
  Nan::Callback* callback = new Nan::Callback(Nan::To<v8::Function>(info[1]).ToLocalChecked());

  AsyncQueueWorker(new InitializeServerWorker(service, callback));
}

NAN_MODULE_INIT(Init) {
  // Custom types
  KerberosClient::Init(target);
  KerberosServer::Init(target);

  Nan::Set(target, Nan::New("initializeClient").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(InitializeClient)).ToLocalChecked());
  Nan::Set(target, Nan::New("initializeServer").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(InitializeServer)).ToLocalChecked());
}

NODE_MODULE(kerberos, Init)
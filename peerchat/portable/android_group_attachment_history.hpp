#pragma once
#include "../group_attachment_history.hpp"
#include "android_storage_provider.hpp"
#include "attachment_file_io.hpp"
namespace peerchat::group_attachments::android {
inline std::unique_ptr<PortableHistoryStore> openHistory(const Profile& profile,std::shared_ptr<transfer::posix::Cache> cache){
 auto identity=std::dynamic_pointer_cast<platform::android::Identity>(profile.identity);protocol::check(identity&&cache);
 auto source=identity->context;platform::android::Context::Call call(source->vm);
 auto facade=call.env->CallObjectMethod(source->object,source->method(call.env,"groupAttachmentHistoryProvider","()Ljava/lang/Object;"));
 platform::android::Context::checked(call.env);protocol::check(facade!=nullptr);
 auto context=std::make_shared<platform::android::Context>(call.env,facade);
 auto database=std::make_shared<platform::android::Database>(context);
 auto records=std::make_shared<platform::android::Records>(context);
 return std::make_unique<PortableHistoryStore>(database,records,"PeerChat/AndroidGroupAttachmentHistory/v1/"+profile.peerId,
  [cache](size_t bytes){cache->reserve(bytes);},PortableHistoryStore::DefaultMaximumBytes,true);
}
}

//
// k8s_replicaset_handler.cpp
//

#include "k8s_replicaset_handler.h"
#include "sinsp.h"
#include "sinsp_int.h"

// filters normalize state and event JSONs, so they can be processed generically:
// event is turned into a single-entry array, state is turned into an array of ADDED events

std::string k8s_replicaset_handler::EVENT_FILTER =
	"{"
	" type: .type,"
	" apiVersion: .object.apiVersion,"
	" kind: .object.kind,"
	" items:"
	" ["
	"  .object |"
	"  {"
	"   namespace: .metadata.namespace,"
	"   name: .metadata.name,"
	"   uid: .metadata.uid,"
	"   timestamp: .metadata.creationTimestamp,"
	"   specReplicas: .spec.replicas,"
	"   statReplicas: .status.replicas,"
	"   selector: .spec.selector.matchLabels,"
	"   labels: .metadata.labels"
	"  }"
	" ]"
	"}";

std::string k8s_replicaset_handler::STATE_FILTER =
	"{"
	" type: \"ADDED\","
	" apiVersion: .apiVersion,"
	" kind: \"ReplicaSet\", "
	" items:"
	" ["
	"  .items[] | "
	"  {"
	"   namespace: .metadata.namespace,"
	"   name: .metadata.name,"
	"   uid: .metadata.uid,"
	"   timestamp: .metadata.creationTimestamp,"
	"   specReplicas: .spec.replicas,"
	"   statReplicas: .status.replicas,"
	"   selector: .spec.selector.matchLabels,"
	"   labels: .metadata.labels"
	"   }"
	" ]"
	"}";

k8s_replicaset_handler::k8s_replicaset_handler(k8s_state_t& state,
	ptr_t dependency_handler,
	collector_ptr_t collector,
	std::string url,
	const std::string& http_version,
	ssl_ptr_t ssl,
	bt_ptr_t bt,
	bool connect):
		k8s_handler("k8s_replicaset_handler", true,
					url, "/apis/extensions/v1beta1/replicasets",
					STATE_FILTER, EVENT_FILTER, dependency_handler, collector,
					http_version, 1000L, ssl, bt, &state, true, connect)
{
}

k8s_replicaset_handler::~k8s_replicaset_handler()
{
}

bool k8s_replicaset_handler::handle_component(const Json::Value& json, const msg_data* data)
{
	if(data)
	{
		if(m_state)
		{
			if((data->m_reason == k8s_component::COMPONENT_ADDED) ||
			   (data->m_reason == k8s_component::COMPONENT_MODIFIED))
			{
				k8s_rs_t& rs =
					m_state->get_component<k8s_replicasets, k8s_rs_t>(m_state->get_rss(),
																	  data->m_name, data->m_uid, data->m_namespace);
				k8s_pair_list entries = k8s_component::extract_object(json, "labels");
				if(entries.size() > 0)
				{
					rs.set_labels(std::move(entries));
				}
				handle_selectors(rs, json["selector"]);
				const Json::Value& spec = json["specReplicas"];
				const Json::Value& stat = json["statReplicas"];
				if(!spec.isNull() && spec.isConvertibleTo(Json::intValue) &&
				   !stat.isNull() && stat.isConvertibleTo(Json::intValue))
				{
					rs.set_replicas(spec.asInt(), stat.asInt());
				}
			}
			else if(data->m_reason == k8s_component::COMPONENT_DELETED)
			{
				if(!m_state->delete_component(m_state->get_rss(), data->m_uid))
				{
					log_not_found(*data);
					return false;
				}
			}
			else if(data->m_reason != k8s_component::COMPONENT_ERROR)
			{
				g_logger.log(std::string("Unsupported K8S " + name() + " event reason: ") +
							 std::to_string(data->m_reason), sinsp_logger::SEV_ERROR);
				return false;
			}
		}
		else
		{
			throw sinsp_exception("K8s node handler: state is null.");
		}
	}
	else
	{
		throw sinsp_exception("K8s node handler: data is null.");
	}
	return true;
}

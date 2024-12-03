#include "yaml_json.h"
#include "yaml_json_internal.h"

namespace mesytec::mvlc::util
{

std::string yaml_to_json(const std::string &yamlText)
{
    YAML::Node yRoot = YAML::Load(yamlText);
    return detail::yaml_to_json(yRoot).dump(true);
}

std::string json_to_yaml(const std::string &jsonText)
{
    auto jRoot = nlohmann::json::parse(jsonText);
    auto yRoot = detail::json_to_yaml(jRoot);
    YAML::Emitter yEmitter;
    yEmitter << yRoot;
    return yEmitter.c_str();
}

}

#ifndef A5CCD5F6_F1A0_437B_88FA_A5308490A099
#define A5CCD5F6_F1A0_437B_88FA_A5308490A099

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/emittermanip.h>

namespace mesytec::mvlc::util::detail
{

inline nlohmann::json yaml_to_json(const YAML::Node &yNode)
{
    nlohmann::json jNode;

    switch (yNode.Type())
    {
        case YAML::NodeType::Null:
        case YAML::NodeType::Undefined:
            jNode = nullptr;
            break;

        case YAML::NodeType::Scalar:
            jNode = yNode.as<std::string>();
            break;

        case YAML::NodeType::Sequence:
            for (const auto &yElem: yNode)
                jNode.emplace_back(yaml_to_json(yElem));
            break;

        case YAML::NodeType::Map:
            for (const auto &yPair: yNode)
                jNode[yPair.first.as<std::string>()] = yaml_to_json(yPair.second);
            break;
    }

    return jNode;
}

inline YAML::Node json_to_yaml(const nlohmann::json &jNode)
{
    YAML::Node yNode;

    switch (jNode.type())
    {
        case nlohmann::json::value_t::null:
        case nlohmann::json::value_t::binary:
        case nlohmann::json::value_t::discarded:
            break;

        case nlohmann::json::value_t::array:
            for (const auto &jElem: jNode)
                yNode.push_back(json_to_yaml(jElem));
            break;

        case nlohmann::json::value_t::object:
            for (const auto &jPair: jNode.items())
                yNode[jPair.key()] = json_to_yaml(jPair.value());
            break;

        case nlohmann::json::value_t::string:
            yNode = jNode.get<std::string>();
            break;

        case nlohmann::json::value_t::boolean:
            yNode = jNode.get<bool>();
            break;

        case nlohmann::json::value_t::number_integer:
            yNode = jNode.get<int64_t>();
            break;

        case nlohmann::json::value_t::number_unsigned:
            yNode = jNode.get<uint64_t>();
            break;

        case nlohmann::json::value_t::number_float:
            yNode = jNode.get<double>();
            break;
    }

    return yNode;
}

}

#endif /* A5CCD5F6_F1A0_437B_88FA_A5308490A099 */

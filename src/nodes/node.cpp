#include "node.h"

namespace garadget {

Config& Node::getConfig() { return Config::instance(); }

bool Node::init() { return enabled_; }

void Node::process() {}

bool Node::receive(const Message&) { return true; }

}  // namespace garadget


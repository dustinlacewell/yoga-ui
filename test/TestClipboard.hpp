#pragma once

#include <yui/core/Clipboard.hpp>

#include <string>
#include <utility>

namespace yui::test {

// An in-memory IClipboard for driving Cut/Copy/Paste in the suite without a
// platform. Tracks setText calls so tests can assert the clipboard was (or was
// NOT) written — e.g. the password-copy refusal must leave it untouched.
class TestClipboard : public IClipboard {
public:
    TestClipboard() = default;
    explicit TestClipboard(std::string initial) : text_(std::move(initial)) {}

    std::string getText() override { return text_; }

    void setText(const std::string& text) override {
        text_ = text;
        ++sets_;
    }

    const std::string& text() const { return text_; }
    int sets() const { return sets_; }

private:
    std::string text_;
    int sets_ = 0;
};

}  // namespace yui::test

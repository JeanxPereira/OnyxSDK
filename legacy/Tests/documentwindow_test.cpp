#include <doctest/doctest.h>

#include <Onyx/Viewers/DocumentWindow.h>

#include <memory>
#include <string>

using namespace Onyx::Viewers;

namespace {

// Minimal IDocumentContent stub -- never calls Draw() in these tests, so
// it never needs an ImGui context. Pure (DocumentId, tab) bookkeeping only
// (Task 13a); nothing here touches ImGui/GL/Vulkan.
struct FakeTab : IDocumentContent {
    std::string GetName() const override { return "fake"; }
    void Draw() override {}
};

} // namespace

TEST_CASE("DocumentWindow: AddTab records the (DocumentId, tab) association") {
    DocumentWindow dw;
    auto t1 = std::make_shared<FakeTab>();
    auto t2 = std::make_shared<FakeTab>();
    auto t3 = std::make_shared<FakeTab>();

    dw.AddTab(t1, 10);
    dw.AddTab(t2, 20);
    dw.AddTab(t3, 10);

    CHECK(dw.TabCount() == 3);
    CHECK(dw.TabCountForDocument(10) == 2);
    CHECK(dw.TabCountForDocument(20) == 1);
    CHECK(dw.TabCountForDocument(999) == 0);
}

TEST_CASE("DocumentWindow: AddTab defaults docId to 0 (not associated with any document)") {
    DocumentWindow dw;
    auto t1 = std::make_shared<FakeTab>();

    dw.AddTab(t1); // no docId -- e.g. MinimalViewer's hex-dump tab

    CHECK(dw.TabCount() == 1);
    CHECK(dw.TabCountForDocument(0) == 1);
}

TEST_CASE("DocumentWindow: a null tab is never added") {
    DocumentWindow dw;
    dw.AddTab(nullptr, 10);
    CHECK(dw.TabCount() == 0);
}

TEST_CASE("DocumentWindow: CloseTabsForDocument closes only tabs for that document, "
          "deferred one frame") {
    DocumentWindow dw;
    auto t1 = std::make_shared<FakeTab>();
    auto t2 = std::make_shared<FakeTab>();
    auto t3 = std::make_shared<FakeTab>();

    dw.AddTab(t1, 10);
    dw.AddTab(t2, 20);
    dw.AddTab(t3, 10);

    std::weak_ptr<IDocumentContent> w1 = t1, w2 = t2, w3 = t3;
    t1.reset();
    t3.reset(); // DocumentWindow's own copies are the only owners of these two now

    dw.CloseTabsForDocument(10);

    CHECK(dw.TabCount() == 1); // only t2 (docId 20) remains
    CHECK(dw.TabCountForDocument(10) == 0);
    CHECK(dw.TabCountForDocument(20) == 1);

    // Same one-frame GL/Vulkan grace period as CloseAll()/CloseActiveTab():
    // the closed tabs are still alive in m_pendingDelete, not yet
    // destroyed.
    CHECK_FALSE(w1.expired());
    CHECK_FALSE(w3.expired());
    CHECK_FALSE(w2.expired()); // t2 was never touched

    // Drop the test's own last strong ref to t2 too, so the upcoming
    // Shutdown() check below is actually proving DocumentWindow released
    // it -- not just observing that this local variable is still alive.
    t2.reset();

    // T10's Shutdown() destroys immediately, no grace period -- proves the
    // grace period above was real, not just "still referenced by the
    // test's own t2".
    dw.Shutdown();
    CHECK(w1.expired());
    CHECK(w3.expired());
    CHECK(w2.expired());
}

TEST_CASE("DocumentWindow: CloseTabsForDocument(0) is a no-op -- 0 is the invalid DocumentId") {
    DocumentWindow dw;
    auto t1 = std::make_shared<FakeTab>();
    dw.AddTab(t1, 0);

    dw.CloseTabsForDocument(0);

    CHECK(dw.TabCount() == 1); // untouched -- 0 never matches
}

TEST_CASE("DocumentWindow: CloseTab closes exactly the named tab and is a no-op for "
          "an unrelated or null one") {
    DocumentWindow dw;
    auto t1 = std::make_shared<FakeTab>();
    auto t2 = std::make_shared<FakeTab>();
    dw.AddTab(t1, 10);
    dw.AddTab(t2, 20);

    auto unrelated = std::make_shared<FakeTab>(); // never added to dw
    dw.CloseTab(unrelated.get());
    CHECK(dw.TabCount() == 2);

    dw.CloseTab(nullptr);
    CHECK(dw.TabCount() == 2);

    dw.CloseTab(t1.get());
    CHECK(dw.TabCount() == 1);
    CHECK(dw.TabCountForDocument(10) == 0);
    CHECK(dw.TabCountForDocument(20) == 1);

    // Closing the same tab again is a no-op, not a crash/double-free.
    dw.CloseTab(t1.get());
    CHECK(dw.TabCount() == 1);
}

#ifndef MethodMock_h__
#define MethodMock_h__

#include <vector>
#include <functional>
#include <atomic>
#include <tuple>

#include "mockutils/TupleDispatcher.h"
#include "mock4cpp/InvocationMatcher.h"
#include "mock4cpp/DomainObjects.h"
#include "mock4cpp/ActualInvocation.h"
#include "mock4cpp/Exceptions.h"

namespace mock4cpp {

static std::atomic_int invocationOrdinal;

template<typename R, typename ... arglist>
struct BehaviorMock {
	virtual R invoke(const arglist&... args) = 0;
};

template<typename R, typename ... arglist>
struct DoMock: public BehaviorMock<R, arglist...> {
	DoMock(std::function<R(arglist...)> f) :
			f(f) {
	}
	virtual R invoke(const arglist&... args) override {
		return f(args...);
	}
private:
	std::function<R(arglist...)> f;
};

template<typename R, typename ... arglist>
struct MethodInvocationMock: public InvocationMatcher<arglist...>, public MethodInvocationHandler<R, arglist...> {

};

template<typename R, typename ... arglist>
struct RecordedMethodBody: public MethodInvocationHandler<R, arglist...> {

	void append(std::shared_ptr<BehaviorMock<R, arglist...>> mock) {
		behaviorMocks.push_back(mock);
	}

	void appendDo(std::function<R(arglist...)> method) {
		auto doMock = std::shared_ptr<BehaviorMock<R, arglist...>> { new DoMock<R, arglist...>(method) };
		append(doMock);
	}

	void clear() {
		behaviorMocks.clear();
	}

	R handleMethodInvocation(const arglist&... args) override {
		std::shared_ptr<BehaviorMock<R, arglist...>> behavior = behaviorMocks.front();
		if (behaviorMocks.size() > 1)
			behaviorMocks.erase(behaviorMocks.begin());
		return behavior->invoke(args...);
	}

private:
	std::vector<std::shared_ptr<BehaviorMock<R, arglist...>>>behaviorMocks;
};

template<typename R, typename ... arglist>
struct MethodInvocationMockBase: public virtual MethodInvocationMock<R, arglist...> {

	MethodInvocationMockBase(const Method& method, std::shared_ptr<InvocationMatcher<arglist...>> matcher,
			std::shared_ptr<MethodInvocationHandler<R, arglist...>> invocationHandler) :
			method(method), matcher { matcher }, invocationHandler { invocationHandler } {
	}

	R handleMethodInvocation(const arglist&... args) override {
		return invocationHandler->handleMethodInvocation(args...);
	}

	virtual bool matches(ActualInvocation<arglist...>& actualInvocation) {
		return matcher->matches(actualInvocation);
	}

private:
	const Method& method;
	std::shared_ptr<InvocationMatcher<arglist...>> matcher;
	std::shared_ptr<MethodInvocationHandler<R, arglist...>> invocationHandler;
};

template<typename ... arglist>
struct ExpectedArgumentsInvocationMatcher: public InvocationMatcher<arglist...> {
	ExpectedArgumentsInvocationMatcher(const arglist&... args) :
			expectedArguments(args...) {
	}

	virtual bool matches(ActualInvocation<arglist...>& invocation) override {
		return matches(invocation.getActualArguments());
	}
private:
	virtual bool matches(const std::tuple<arglist...>& actualArgs) {
		return expectedArguments == actualArgs;
	}
	const std::tuple<arglist...> expectedArguments;
};

template<typename ... arglist>
struct UserDefinedInvocationMatcher: public InvocationMatcher<arglist...> {
	UserDefinedInvocationMatcher(std::function<bool(arglist...)> matcher) :
			matcher { matcher } {
	}

	virtual bool matches(ActualInvocation<arglist...>& invocation) override {
		return matches(invocation.getActualArguments());
	}
private:
	virtual bool matches(const std::tuple<arglist...>& actualArgs) {
		return invoke<arglist...>(matcher, std::tuple<arglist...> { actualArgs });
	}
	std::function<bool(arglist...)> matcher;
};

template<typename ... arglist>
struct DefaultInvocationMatcher: public InvocationMatcher<arglist...> {
	DefaultInvocationMatcher() {
	}

	virtual bool matches(ActualInvocation<arglist...>& invocation) override {
		return matches(invocation.getActualArguments());
	}

private:
	virtual bool matches(const std::tuple<arglist...>& actualArgs) {
		return true;
	}
};

template<typename C, typename R, typename ... arglist>
struct MethodMock: public virtual Method, public virtual MethodInvocationHandler<R, arglist...>
, public virtual ActualInvocationsSource
{
	MethodMock(MockObject& mock, R(C::*vMethod)(arglist...)) :
			mock(mock),vMethod(vMethod) {
	}

	virtual ~MethodMock() override {
	}

	std::string getMethodName() const override {
		return typeid(vMethod).name();
	}

	virtual MockObject& getMockObject() override {
		return mock;
	}

	void stubMethodInvocation(std::shared_ptr<InvocationMatcher<arglist...>> invocationMatcher,
			std::shared_ptr<MethodInvocationHandler<R, arglist...>> invocationHandler) {
		auto mock = buildMethodInvocationMock(invocationMatcher, invocationHandler);
		methodInvocationMocks.push_back(mock);
	}

	void clear() {
		methodInvocationMocks.clear();
	}

	R handleMethodInvocation(const arglist&... args) override {
		int ordinal = invocationOrdinal++;
		auto actualInvoaction = std::shared_ptr<ActualInvocation<arglist...>> { new ActualInvocation<arglist...>(ordinal, *this, args...) };
		actualInvocations.push_back(actualInvoaction);
		auto methodInvocationMock = getMethodInvocationMockForActualArgs(*actualInvoaction);
		if (!methodInvocationMock) {
			throw UnmockedMethodCallException();
		}
		return methodInvocationMock->handleMethodInvocation(args...);
	}

	std::vector<std::shared_ptr<ActualInvocation<arglist...>> > getActualInvocations(InvocationMatcher<arglist...>& matcher) {
		std::vector < std::shared_ptr<ActualInvocation<arglist...>> > result;
		for (auto actualInvocation : actualInvocations) {
			if (matcher.matches(*actualInvocation)) {
				result.push_back(actualInvocation);
			}
		}
		return result;
	}

	void getActualInvocations(std::unordered_set<AnyInvocation*>& into) const {
		for (auto invocation : actualInvocations){
			into.insert(invocation.get());
		}
	}
private:

	MockObject& mock;
	R(C::*vMethod)(arglist...);
	std::vector<std::shared_ptr<MethodInvocationMock<R, arglist...>>>methodInvocationMocks;
	std::vector<std::shared_ptr<ActualInvocation<arglist...>>> actualInvocations;

	std::shared_ptr<MethodInvocationMockBase<R, arglist...>> buildMethodInvocationMock(
			std::shared_ptr<InvocationMatcher<arglist...>> invocationMatcher,
			std::shared_ptr<MethodInvocationHandler<R, arglist...>> invocationHandler) {
		return std::shared_ptr<MethodInvocationMockBase<R, arglist...>> {new MethodInvocationMockBase<R, arglist...>(*this, invocationMatcher,
					invocationHandler)};
	}

	std::shared_ptr<MethodInvocationMock<R, arglist...>> getMethodInvocationMockForActualArgs(ActualInvocation<arglist...>& invocation) {
		for (auto i = methodInvocationMocks.rbegin(); i != methodInvocationMocks.rend(); ++i) {
			if ((*i)->matches(invocation)) {
				return (*i);
			}
		}
		return nullptr;
	}

};

}
#endif // MethodMock_h__
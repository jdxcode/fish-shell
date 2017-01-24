// Handles IO that may hang.
#ifndef FISH_IOTHREAD_H
#define FISH_IOTHREAD_H

#include <functional>

/// Runs a command on a thread.
///
/// \param handler The function to execute on a background thread. Accepts an arbitrary context
/// pointer, and returns an int, which is passed to the completionCallback.
/// \param completionCallback The function to execute on the main thread once the background thread
/// is complete. Accepts an int (the return value of handler) and the context.
/// \param context A arbitary context pointer to pass to the handler and completion callback.
/// \return A sequence number, currently not very useful.
int iothread_perform_base(int (*handler)(void *), void (*completionCallback)(void *, int),
                          void *context);

/// Gets the fd on which to listen for completion callbacks.
///
/// \return A file descriptor on which to listen for completion callbacks.
int iothread_port(void);

/// Services one iothread competion callback.
void iothread_service_completion(void);

/// Waits for all iothreads to terminate.
void iothread_drain_all(void);

// Internal implementation
int iothread_perform_impl(std::function<void(void)> &&func,
                          std::function<void(void)> &&completion);

// Template helpers
template<typename T>
struct _iothread_trampoline {
    template<typename HANDLER, typename COMPLETION>
    static int perform(const HANDLER &handler, const COMPLETION &completion) {
        T *result = new T();
        return iothread_perform_impl([=](){ *result = handler(); },
                                     [=](){ completion(std::move(*result)); delete result; });
    }
};


// Void specialization
template<>
struct _iothread_trampoline<void> {
    template<typename HANDLER, typename COMPLETION>
    static int perform(const HANDLER &handler, const COMPLETION &completion) {
        return iothread_perform_impl(handler, completion);
    }
};

// iothread_perform invokes a handler on a background thread, and then a completion function
// on the main thread. The value returned from the handler is passed to the completion.
template<typename HANDLER, typename COMPLETION>
int iothread_perform(const HANDLER &handler, const COMPLETION &completion) {
    return _iothread_trampoline<decltype(handler())>::perform(handler, completion);
}

// variant of iothread_perform without a completion handler
inline int iothread_perform(std::function<void(void)> &&func) {
    return iothread_perform_impl(std::move(func),
                                 std::function<void(void)>());
}

/// Legacy templates
template <typename T>
int iothread_perform(int (*handler)(T *), void (*completion)(T *, int), T *context) {
    return iothread_perform(std::function<int(void)>([=](){return handler(context);}),
                            std::function<void(int)>([=](int v){completion(context, v);})
                            );
}

template <typename T>
int iothread_perform(int (*handler)(T *), T *context) {
    return iothread_perform([=](){ handler(context); });
}

/// Performs a function on the main thread, blocking until it completes.
void iothread_perform_on_main(std::function<void(void)> &&func);

#endif

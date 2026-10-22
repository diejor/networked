using System;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using Godot;

namespace Networked;

/// <summary>
/// The failure of an awaited <see cref="NetwPromise"/>, with its code and
/// reason.
/// </summary>
/// <remarks>
/// Throw one from a task given to <see cref="NetwPromise.FromTask(Task)"/>
/// to fail the promise with that code.
/// </remarks>
public sealed class NetwException : Exception
{
    public NetwException(Error code, string detail = "")
        : base(string.IsNullOrEmpty(detail) ? code.ToString() : detail)
    {
        Code = code;
        Detail = detail ?? "";
    }

    /// <summary>
    /// The error the promise failed with.
    /// </summary>
    public Error Code { get; }

    /// <summary>
    /// A readable reason for <see cref="Code"/>, or empty.
    /// </summary>
    public string Detail { get; }
}

internal static class NetwSettle
{
    public static void WhenSettled(
        NetwObject promise,
        bool settled,
        Action action)
    {
        if (settled)
        {
            action();
            return;
        }
        promise.Connect(
            "settled",
            Callable.From(action),
            (long)GodotObject.ConnectFlags.OneShot);
    }

    public static void Resume(
        NetwObject promise,
        bool settled,
        Action continuation)
    {
        SynchronizationContext context = SynchronizationContext.Current;
        WhenSettled(promise, settled, () => Post(context, continuation));
    }

    private static void Post(
        SynchronizationContext context,
        Action continuation)
    {
        if (context == null)
        {
            Callable.From(continuation).CallDeferred();
            return;
        }
        context.Post(state => ((Action)state)(), continuation);
    }

    public static Exception Failure(Error code, string detail)
    {
        return code == Error.Skip
            ? new OperationCanceledException(detail)
            : new NetwException(code, detail);
    }

    public static void Fail(NetwPromise promise, Exception failure)
    {
        switch (failure)
        {
            case NetwException refused:
                promise.Reject(refused.Code, refused.Detail);
                break;
            case OperationCanceledException:
                promise.Reject(Error.Skip);
                break;
            default:
                promise.Reject(Error.Bug, failure.Message);
                break;
        }
    }
}

public sealed partial class NetwPromise
{
    /// <summary>
    /// Lets C# <c>await</c> the promise. The value comes back, and a failure
    /// throws <see cref="NetwException"/>, or
    /// <see cref="OperationCanceledException"/> for
    /// <see cref="Error.Skip"/>.
    /// </summary>
    public Awaiter GetAwaiter()
    {
        return new Awaiter(this);
    }

    /// <summary>
    /// What <c>await</c> uses on a <see cref="NetwPromise"/>. The code after
    /// the <c>await</c> runs on the thread that awaited, once the promise has
    /// settled.
    /// </summary>
    public readonly struct Awaiter : INotifyCompletion
    {
        private readonly NetwPromise _promise;

        internal Awaiter(NetwPromise promise)
        {
            _promise = promise;
        }

        /// <summary>
        /// <see cref="NetwPromise.IsSettled"/>, so a settled promise does not
        /// wait.
        /// </summary>
        public bool IsCompleted => _promise.IsSettled;

        /// <summary>
        /// Runs <paramref name="continuation"/> once the promise settles.
        /// </summary>
        public void OnCompleted(Action continuation)
        {
            NetwSettle.Resume(_promise, _promise.IsSettled, continuation);
        }

        /// <summary>
        /// The <see cref="NetwPromise.Result"/>, or the failure thrown.
        /// </summary>
        public Variant GetResult()
        {
            if (!_promise.IsFailed)
            {
                return _promise.Result;
            }
            throw NetwSettle.Failure(_promise.Code, _promise.Detail);
        }
    }

    /// <summary>
    /// Returns a <see cref="Task{T}"/> that completes with the value as
    /// <typeparamref name="T"/>, for <see cref="Task.WhenAll(Task[])"/> and
    /// anything else built on <see cref="Task"/>. Pass a converter for a
    /// Networked type, as in <c>AsTask(NetwScene.From)</c>.
    /// </summary>
    public Task<T> AsTask<[MustBeVariant] T>()
    {
        return AsTask(value => value.As<T>());
    }

    /// <summary>
    /// Returns a <see cref="Task{T}"/> that completes with the value passed
    /// through <paramref name="convert"/>.
    /// <code>
    /// NetwScene arena = await promise.AsTask(NetwScene.From);
    /// </code>
    /// </summary>
    public Task<T> AsTask<T>(Func<Variant, T> convert)
    {
        var source = new TaskCompletionSource<T>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        NetwSettle.WhenSettled(this, IsSettled, () =>
        {
            if (IsFailed)
            {
                if (Code == Error.Skip)
                {
                    source.TrySetCanceled();
                    return;
                }
                source.TrySetException(new NetwException(Code, Detail));
                return;
            }
            try
            {
                source.TrySetResult(convert(Result));
            }
            catch (Exception failure)
            {
                source.TrySetException(failure);
            }
        });
        return source.Task;
    }

    /// <summary>
    /// Returns a promise that completes with <c>null</c> when
    /// <paramref name="task"/> finishes, and fails when it throws.
    /// </summary>
    public static NetwPromise FromTask(Task task)
    {
        var promise = new NetwPromise();
        Settle(promise, task);
        return promise;
    }

    /// <summary>
    /// Returns a promise that completes with the result of
    /// <paramref name="task"/>. A thrown <see cref="NetwException"/> fails it
    /// with that code, a cancellation with <see cref="Error.Skip"/>, and any
    /// other exception with <see cref="Error.Bug"/> and its message.
    /// An RPC method returns it as a <see cref="Variant"/>, because Godot
    /// cannot call a method that returns <see cref="Task"/>.
    /// <code>
    /// public Variant PreloadLevel(string path) =&gt;
    ///     NetwPromise.FromTask(Preload(path));
    /// </code>
    /// </summary>
    public static NetwPromise FromTask<[MustBeVariant] T>(Task<T> task)
    {
        var promise = new NetwPromise();
        Settle(promise, task);
        return promise;
    }

    private static async void Settle(NetwPromise promise, Task task)
    {
        try
        {
            await task;
        }
        catch (Exception failure)
        {
            NetwSettle.Fail(promise, failure);
            return;
        }
        promise.Resolve(default);
    }

    private static async void Settle<[MustBeVariant] T>(
        NetwPromise promise,
        Task<T> task)
    {
        T value;
        try
        {
            value = await task;
        }
        catch (Exception failure)
        {
            NetwSettle.Fail(promise, failure);
            return;
        }
        promise.Resolve(Variant.From(value));
    }
}

public sealed partial class NetwGroupPromise
{
    /// <summary>
    /// Lets C# <c>await</c> the group. The settled group comes back and
    /// nothing is thrown, so read <see cref="IsFailed"/> and
    /// <see cref="Results"/> after it.
    /// </summary>
    public Awaiter GetAwaiter()
    {
        return new Awaiter(this);
    }

    /// <summary>
    /// What <c>await</c> uses on a <see cref="NetwGroupPromise"/>. The code
    /// after the <c>await</c> runs on the thread that awaited, once the group
    /// has settled.
    /// </summary>
    public readonly struct Awaiter : INotifyCompletion
    {
        private readonly NetwGroupPromise _group;

        internal Awaiter(NetwGroupPromise group)
        {
            _group = group;
        }

        /// <summary>
        /// <see cref="NetwGroupPromise.IsSettled"/>, so a settled group does
        /// not wait.
        /// </summary>
        public bool IsCompleted => _group.IsSettled;

        /// <summary>
        /// Runs <paramref name="continuation"/> once the group settles.
        /// </summary>
        public void OnCompleted(Action continuation)
        {
            NetwSettle.Resume(_group, _group.IsSettled, continuation);
        }

        /// <summary>
        /// The settled group.
        /// </summary>
        public NetwGroupPromise GetResult()
        {
            return _group;
        }
    }
}

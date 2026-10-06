namespace EnglishApp.Services;

public static class AppPaths
{
    public static string BaseDir => AppContext.BaseDirectory;
    public static string UserDir { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EnglishApp");
    public static string RecordingsDir => Path.Combine(UserDir, "recordings");
    public static string UserDb => Path.Combine(UserDir, "user.db");
    public static string ContentDb => Path.Combine(BaseDir, "content.db");
    public static string UserSchema => Path.Combine(BaseDir, "user_schema.sql");
    public static string ModelsDir => Path.Combine(BaseDir, "models");
}
